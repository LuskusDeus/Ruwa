// SPDX-License-Identifier: MPL-2.0

#include "features/effects/plugin/EffectPluginManager.h"

#include "features/effects/GLLayerEffectRenderRegistry.h"
#include "features/effects/LayerEffectRegistry.h"
#include "features/effects/plugin/EffectAbiAdapter.h"
#include "features/effects/plugin/EffectPluginPass.h"

#include <ruwa/effect/ruwa_effect_sdk.h>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QLibrary>
#include <QStandardPaths>

#include <cstddef>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace ruwa::core::effects::plugin {

namespace {

template <typename Struct, typename Member>
constexpr std::size_t fieldEnd(std::size_t offset, Member Struct::*)
{
    return offset + sizeof(Member);
}

#define RUWA_ABI_FIELD_END(type, field) fieldEnd(offsetof(type, field), &type::field)

// These are the complete ABI 1.0 prefixes. Later ABI-minor revisions may only
// append fields, so a newer host accepts these sizes and never reads past them.
constexpr std::size_t kMinPluginStruct = RUWA_ABI_FIELD_END(RuwaEffectPluginApi, effects);
constexpr std::size_t kMinEffectStruct = RUWA_ABI_FIELD_END(RuwaEffectDescriptor, destroy_pass);
constexpr std::size_t kMinCapabilitiesStruct
    = RUWA_ABI_FIELD_END(RuwaEffectCapabilities, reads_whole_layer);
constexpr std::size_t kMinParamStruct = RUWA_ABI_FIELD_END(RuwaEffectParamDef, default_binding);

#undef RUWA_ABI_FIELD_END

template <typename T> const T* abiArrayElement(const T* first, uint32_t index)
{
    if (!first || first->struct_size < sizeof(uint32_t)) {
        return nullptr;
    }
    if (index > std::numeric_limits<std::size_t>::max() / first->struct_size) {
        return nullptr;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(first);
    return reinterpret_cast<const T*>(bytes + static_cast<std::size_t>(index) * first->struct_size);
}

bool validStableKey(const char* text, bool requireDot)
{
    if (!text || text[0] == '\0') {
        return false;
    }
    bool hasDot = false;
    std::size_t length = 0;
    for (; text[length] != '\0' && length <= 255; ++length) {
        const unsigned char c = static_cast<unsigned char>(text[length]);
        const bool alphaNumeric
            = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alphaNumeric && c != '_' && c != '-' && c != '.') {
            return false;
        }
        hasDot = hasDot || c == '.';
    }
    return length > 0 && length <= 255 && (!requireDot || hasDot);
}

bool isAbiBool(RuwaBool value)
{
    return value == RUWA_FALSE || value == RUWA_TRUE;
}

bool validParamType(RuwaEffectParamType type)
{
    return type >= RUWA_EFFECT_PARAM_BOOL && type <= RUWA_EFFECT_PARAM_CHOICE;
}

bool validEditor(RuwaEffectParamEditor editor)
{
    return editor == RUWA_EFFECT_EDITOR_SLIDER || editor == RUWA_EFFECT_EDITOR_NUMBER_FIELD;
}

bool validAxis(RuwaEffectPositionAxis axis)
{
    return axis == RUWA_EFFECT_AXIS_X || axis == RUWA_EFFECT_AXIS_Y;
}

bool validBinding(RuwaEffectDefaultBinding binding)
{
    return binding >= RUWA_EFFECT_BIND_NONE && binding <= RUWA_EFFECT_BIND_CANVAS_HALF_HEIGHT;
}

bool validateCapabilities(const RuwaEffectCapabilities& caps, QString& error)
{
    if (caps.struct_size < kMinCapabilitiesStruct) {
        error = QStringLiteral("capabilities struct_size too small");
        return false;
    }
    const RuwaBool values[] = { caps.supports_document_tile, caps.supports_viewport_screen,
        caps.expands_bounds, caps.requires_neighbor_tiles, caps.requires_backdrop,
        caps.order_dependent, caps.reads_whole_layer };
    for (RuwaBool value : values) {
        if (!isAbiBool(value)) {
            error = QStringLiteral("capability booleans must be RUWA_FALSE or RUWA_TRUE");
            return false;
        }
    }
    if (caps.supports_document_tile == RUWA_FALSE && caps.supports_viewport_screen == RUWA_FALSE) {
        error = QStringLiteral("effect supports no evaluation space");
        return false;
    }
    if (caps.reads_whole_layer != RUWA_FALSE && caps.requires_neighbor_tiles == RUWA_FALSE) {
        error = QStringLiteral("reads_whole_layer requires requires_neighbor_tiles");
        return false;
    }
    if (caps.reads_whole_layer != RUWA_FALSE && caps.requires_backdrop != RUWA_FALSE) {
        error = QStringLiteral(
            "reads_whole_layer and requires_backdrop cannot be combined in ABI v1");
        return false;
    }
    return true;
}

bool validateParam(const RuwaEffectParamDef* def, uint32_t index, QSet<QString>& keys,
    QHash<QString, uint32_t>& positionAxes, QString& error)
{
    if (!def || def->struct_size < kMinParamStruct) {
        error = QStringLiteral("parameter %1 struct_size too small").arg(index);
        return false;
    }
    if (!validStableKey(def->key, false)) {
        error = QStringLiteral("parameter %1 has an invalid key").arg(index);
        return false;
    }
    const QString key = QString::fromUtf8(def->key);
    if (keys.contains(key)) {
        error = QStringLiteral("duplicate parameter key: %1").arg(key);
        return false;
    }
    keys.insert(key);
    if (!validParamType(def->type) || !validEditor(def->preferred_editor)
        || !validAxis(def->position_axis) || !validBinding(def->default_binding)) {
        error = QStringLiteral("parameter %1 contains an unknown enum value").arg(key);
        return false;
    }
    if (def->type == RUWA_EFFECT_PARAM_BOOL && def->default_value != 0.0
        && def->default_value != 1.0) {
        error = QStringLiteral("boolean parameter %1 default must be 0 or 1").arg(key);
        return false;
    }
    if (def->type == RUWA_EFFECT_PARAM_INT || def->type == RUWA_EFFECT_PARAM_REAL) {
        if (!std::isfinite(def->default_value) || !std::isfinite(def->min_value)
            || !std::isfinite(def->max_value) || !std::isfinite(def->step_value)
            || def->min_value > def->max_value || def->step_value <= 0.0
            || def->default_value < def->min_value || def->default_value > def->max_value) {
            error
                = QStringLiteral("numeric parameter %1 has an invalid range/default/step").arg(key);
            return false;
        }
    }
    if (def->type == RUWA_EFFECT_PARAM_CHOICE) {
        if (!def->choices || def->choice_count == 0 || def->choice_count > 4096
            || def->default_choice < 0
            || static_cast<uint32_t>(def->default_choice) >= def->choice_count) {
            error = QStringLiteral("choice parameter %1 has an invalid choice table/default")
                        .arg(key);
            return false;
        }
        QSet<QString> choices;
        for (uint32_t i = 0; i < def->choice_count; ++i) {
            if (!def->choices[i] || def->choices[i][0] == '\0') {
                error = QStringLiteral("choice parameter %1 contains an empty choice").arg(key);
                return false;
            }
            const QString choice = QString::fromUtf8(def->choices[i]);
            if (choices.contains(choice)) {
                error = QStringLiteral("choice parameter %1 contains duplicate choice %2")
                            .arg(key, choice);
                return false;
            }
            choices.insert(choice);
        }
    }
    const bool hasPosition = def->position_pair_key && def->position_pair_key[0] != '\0';
    if (hasPosition) {
        if (def->type != RUWA_EFFECT_PARAM_REAL) {
            error = QStringLiteral("position parameter %1 must have Real type").arg(key);
            return false;
        }
        if (!validStableKey(def->position_pair_key, false)) {
            error = QStringLiteral("position parameter %1 has an invalid pair key").arg(key);
            return false;
        }
        const QString pairKey = QString::fromUtf8(def->position_pair_key);
        const uint32_t axisBit = def->position_axis == RUWA_EFFECT_AXIS_X ? 1u : 2u;
        if (positionAxes.value(pairKey, 0u) & axisBit) {
            error = QStringLiteral("position pair %1 declares the same axis twice").arg(pairKey);
            return false;
        }
        positionAxes[pairKey] |= axisBit;
    } else if (def->default_binding != RUWA_EFFECT_BIND_NONE) {
        error = QStringLiteral("parameter %1 has a canvas binding but no position pair").arg(key);
        return false;
    }
    return true;
}

QStringList pluginNameFilters()
{
#if defined(Q_OS_WIN)
    return { QStringLiteral("*.dll") };
#elif defined(Q_OS_MAC)
    return { QStringLiteral("*.dylib") };
#else
    return { QStringLiteral("*.so") };
#endif
}

bool validateEffect(const RuwaEffectDescriptor* effect, const QSet<QString>& seenInPlugin,
    QString& typeIdOut, QString& error)
{
    if (!effect) {
        error = QStringLiteral("null effect descriptor");
        return false;
    }
    if (effect->struct_size < kMinEffectStruct) {
        error = QStringLiteral("effect struct_size too small");
        return false;
    }
    if (!validStableKey(effect->type_id, true)) {
        error = QStringLiteral("invalid type_id");
        return false;
    }
    if (effect->version == 0) {
        error = QStringLiteral("effect version must be at least 1");
        return false;
    }
    typeIdOut = QString::fromUtf8(effect->type_id);
    if (seenInPlugin.contains(typeIdOut)) {
        error = QStringLiteral("duplicate type_id within plugin: %1").arg(typeIdOut);
        return false;
    }
    if (LayerEffectRegistry::instance().contains(typeIdOut)
        || aether::GLLayerEffectRenderRegistry::instance().contains(typeIdOut)) {
        error = QStringLiteral("type_id already registered: %1").arg(typeIdOut);
        return false;
    }
    if (!effect->create_pass || !effect->render_pass || !effect->destroy_pass) {
        error = QStringLiteral("missing GPU pass callbacks");
        return false;
    }
    if (effect->param_count > 0 && !effect->params) {
        error = QStringLiteral("param_count > 0 but params is null");
        return false;
    }
    if (effect->param_count > 4096) {
        error = QStringLiteral("param_count exceeds the ABI safety limit");
        return false;
    }
    if (!effect->capabilities) {
        error = QStringLiteral("capabilities is null");
        return false;
    }
    if (!validateCapabilities(*effect->capabilities, error)) {
        return false;
    }
    QSet<QString> paramKeys;
    QHash<QString, uint32_t> positionAxes;
    const uint32_t paramStride = effect->params ? effect->params->struct_size : 0;
    for (uint32_t i = 0; i < effect->param_count; ++i) {
        const RuwaEffectParamDef* def = abiArrayElement(effect->params, i);
        if (!def || def->struct_size != paramStride) {
            error = QStringLiteral("parameter descriptors use inconsistent struct_size values");
            return false;
        }
        if (!validateParam(def, i, paramKeys, positionAxes, error)) {
            return false;
        }
    }
    for (auto it = positionAxes.cbegin(); it != positionAxes.cend(); ++it) {
        if (it.value() != 3u) {
            error
                = QStringLiteral("position pair %1 must contain exactly one X and one Y parameter")
                      .arg(it.key());
            return false;
        }
    }
    return true;
}

bool hasPluginShutdown(const RuwaEffectPluginApi* api)
{
    return api
        && api->struct_size >= offsetof(RuwaEffectPluginApi, shutdown) + sizeof(api->shutdown)
        && api->shutdown;
}

void shutdownPlugin(const RuwaEffectPluginApi* api, const QString& path)
{
    if (!hasPluginShutdown(api)) {
        return;
    }
    try {
        api->shutdown();
    } catch (...) {
        qWarning("Ruwa effect plugin: shutdown threw an exception for '%s'", qPrintable(path));
    }
}

} // namespace

EffectPluginManager& EffectPluginManager::instance()
{
    static EffectPluginManager manager;
    return manager;
}

EffectPluginManager::~EffectPluginManager()
{
    // The manager is created after both registries, therefore it is destroyed
    // before them. Remove every callback carrying a DLL pointer before shutdown
    // and unload. Renderers/pass instances are owned by application widgets and
    // have already been destroyed when the process-level singleton tears down.
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
        for (const QString& typeId : it->typeIds) {
            aether::GLLayerEffectRenderRegistry::instance().unregisterFactory(typeId);
            LayerEffectRegistry::instance().unregisterDescriptor(typeId);
            m_effectsByType.remove(typeId);
        }
        shutdownPlugin(it->api, it->path);
        if (it->library) {
            it->library->unload();
            delete it->library;
            it->library = nullptr;
        }
    }
}

void EffectPluginManager::loadStandardAndUserPlugins()
{
    // Standard packages ship next to the executable; user plugins live in the
    // per-user app-data location. Missing directories load nothing.
    loadDirectory(QCoreApplication::applicationDirPath() + QStringLiteral("/effects"));

    const QString userRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!userRoot.isEmpty()) {
        loadDirectory(userRoot + QStringLiteral("/effects"));
    }
}

int EffectPluginManager::loadDirectory(const QString& directory)
{
    QDir dir(directory);
    if (!dir.exists()) {
        return 0;
    }
    if (m_effectDirsScanned.contains(dir.absolutePath())) {
        return 0;
    }
    m_effectDirsScanned.insert(dir.absolutePath());

    int loaded = 0;
    const QFileInfoList entries
        = dir.entryInfoList(pluginNameFilters(), QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (loadPluginFile(entry.absoluteFilePath())) {
            ++loaded;
        }
    }
    return loaded;
}

bool EffectPluginManager::loadPluginFile(const QString& filePath)
{
    auto library = std::make_unique<QLibrary>(filePath);
    if (!library->load()) {
        qWarning("Ruwa effect plugin: failed to load '%s': %s", qPrintable(filePath),
            qPrintable(library->errorString()));
        return false;
    }

    auto query
        = reinterpret_cast<RuwaEffectQueryFn>(library->resolve(RUWA_EFFECT_QUERY_SYMBOL_NAME));
    if (!query) {
        qWarning("Ruwa effect plugin: '%s' has no '%s' export", qPrintable(filePath),
            RUWA_EFFECT_QUERY_SYMBOL_NAME);
        library->unload();
        return false;
    }

    const RuwaEffectPluginApi* api = nullptr;
    try {
        api = query(RUWA_EFFECT_ABI_MAJOR, hostApiTable());
    } catch (...) {
        qWarning("Ruwa effect plugin: query threw an exception for '%s'", qPrintable(filePath));
        library->unload();
        return false;
    }
    if (!api) {
        qWarning("Ruwa effect plugin: '%s' declined ABI major %u", qPrintable(filePath),
            RUWA_EFFECT_ABI_MAJOR);
        library->unload();
        return false;
    }
    if (api->struct_size < kMinPluginStruct) {
        qWarning("Ruwa effect plugin: '%s' plugin struct_size too small", qPrintable(filePath));
        library->unload();
        return false;
    }
    if (api->abi_major != RUWA_EFFECT_ABI_MAJOR) {
        qWarning("Ruwa effect plugin: '%s' ABI major %u incompatible with host %u",
            qPrintable(filePath), api->abi_major, RUWA_EFFECT_ABI_MAJOR);
        library->unload();
        return false;
    }

    auto rejectAfterQuery = [&] {
        shutdownPlugin(api, filePath);
        library->unload();
    };

    const QString pluginId = api->plugin_id ? QString::fromUtf8(api->plugin_id) : QString();
    if (!validStableKey(api->plugin_id, true)) {
        qWarning("Ruwa effect plugin: '%s' has an invalid plugin_id", qPrintable(filePath));
        rejectAfterQuery();
        return false;
    }
    if (m_pluginIds.contains(pluginId)) {
        qWarning("Ruwa effect plugin: duplicate plugin_id '%s' (from '%s') rejected",
            qPrintable(pluginId), qPrintable(filePath));
        rejectAfterQuery();
        return false;
    }
    if (api->effect_count == 0 || api->effect_count > 1024 || !api->effects) {
        qWarning("Ruwa effect plugin: '%s' exposes no effects", qPrintable(filePath));
        rejectAfterQuery();
        return false;
    }

    // Transactional pre-check: validate every effect before touching a registry.
    QSet<QString> seen;
    std::vector<ruwa::core::effects::LayerEffectDescriptor> descriptors;
    std::vector<const RuwaEffectDescriptor*> effectPtrs;
    descriptors.reserve(api->effect_count);
    effectPtrs.reserve(api->effect_count);
    const uint32_t effectStride = api->effects->struct_size;
    for (uint32_t i = 0; i < api->effect_count; ++i) {
        const RuwaEffectDescriptor* effect = abiArrayElement(api->effects, i);
        QString typeId;
        QString error;
        if (!effect || effect->struct_size != effectStride
            || !validateEffect(effect, seen, typeId, error)) {
            if (error.isEmpty()) {
                error = QStringLiteral("effect descriptors use inconsistent struct_size values");
            }
            qWarning(
                "Ruwa effect plugin: '%s' rejected (%s)", qPrintable(filePath), qPrintable(error));
            rejectAfterQuery();
            return false;
        }
        seen.insert(typeId);
        descriptors.push_back(buildLayerEffectDescriptor(effect));
        effectPtrs.push_back(effect);
    }

    // Commit, rolling back completely on any (unexpected) mid-commit failure.
    QStringList committedDescriptors;
    QStringList committedFactories;
    bool committed = true;
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const QString typeId = descriptors[i].typeId;
        if (!LayerEffectRegistry::instance().registerDescriptor(descriptors[i])) {
            committed = false;
            break;
        }
        committedDescriptors << typeId;
        const RuwaEffectDescriptor* effect = effectPtrs[i];
        auto factory = [effect]() -> std::unique_ptr<aether::IGLLayerEffectPass> {
            return std::make_unique<PluginGLLayerEffectPass>(effect);
        };
        if (!aether::GLLayerEffectRenderRegistry::instance().registerFactory(typeId, factory)) {
            committed = false;
            break;
        }
        committedFactories << typeId;
    }

    if (!committed) {
        for (const QString& typeId : committedFactories) {
            aether::GLLayerEffectRenderRegistry::instance().unregisterFactory(typeId);
        }
        for (const QString& typeId : committedDescriptors) {
            LayerEffectRegistry::instance().unregisterDescriptor(typeId);
        }
        qWarning("Ruwa effect plugin: '%s' registration failed and was rolled back",
            qPrintable(filePath));
        rejectAfterQuery();
        return false;
    }

    for (std::size_t i = 0; i < effectPtrs.size(); ++i) {
        m_effectsByType.insert(descriptors[i].typeId, effectPtrs[i]);
    }
    m_pluginIds.insert(pluginId);
    m_plugins.push_back({ library.release(), api, filePath, committedDescriptors });

    qInfo("Ruwa effect plugin: loaded '%s' (%u effect%s) from '%s'", qPrintable(pluginId),
        api->effect_count, api->effect_count == 1 ? "" : "s", qPrintable(filePath));
    return true;
}

void EffectPluginManager::migrateState(ruwa::core::effects::LayerEffectState& state) const
{
    const auto it = m_effectsByType.constFind(state.typeId);
    if (it == m_effectsByType.cend()) {
        return;
    }
    applyMigrations(it.value(), state);
}

QStringList EffectPluginManager::loadedPluginIds() const
{
    return QStringList(m_pluginIds.cbegin(), m_pluginIds.cend());
}

} // namespace ruwa::core::effects::plugin
