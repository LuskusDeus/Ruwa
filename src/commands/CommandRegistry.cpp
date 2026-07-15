// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================

#include "commands/CommandRegistry.h"
#include "shared/i18n/CommandLocalization.h"
#include <algorithm>

namespace ruwa::core {

CommandRegistry& CommandRegistry::instance()
{
    static CommandRegistry instance;
    return instance;
}

CommandRegistry::CommandRegistry()
    : QObject(nullptr)
{
}

CommandRegistry::~CommandRegistry() = default;

void CommandRegistry::registerCommand(std::unique_ptr<Command> command)
{
    if (!command) {
        return;
    }

    const QString id = command->id();
    const std::string stdId = id.toStdString();

    if (m_commands.find(stdId) != m_commands.end()) {
        return;
    }

    const QString category = command->info().category;

    // Store raw pointer for category index before moving
    Command* rawPtr = command.get();

    m_commands[stdId] = std::move(command);
    m_byCategory[category].append(rawPtr);

    emit commandRegistered(id);
}

Command* CommandRegistry::command(const QString& id) const
{
    const std::string stdId = id.toStdString();
    auto it = m_commands.find(stdId);
    return (it != m_commands.end()) ? it->second.get() : nullptr;
}

bool CommandRegistry::hasCommand(const QString& id) const
{
    const std::string stdId = id.toStdString();
    return m_commands.find(stdId) != m_commands.end();
}

QList<Command*> CommandRegistry::allCommands() const
{
    QList<Command*> result;
    result.reserve(static_cast<int>(m_commands.size()));

    for (const auto& [id, cmd] : m_commands) {
        result.append(cmd.get());
    }

    return result;
}

QList<Command*> CommandRegistry::commandsInCategory(const QString& category) const
{
    return m_byCategory.value(category);
}

QStringList CommandRegistry::categories() const
{
    return m_byCategory.keys();
}

QList<Command*> CommandRegistry::search(const QString& query, int maxResults) const
{
    if (query.isEmpty()) {
        // Return all commands that should appear in palette
        QList<Command*> result;
        for (const auto& [id, cmd] : m_commands) {
            if (cmd->showInPalette()) {
                result.append(cmd.get());
            }
        }
        return result;
    }

    // Score all commands
    QList<std::pair<Command*, int>> scored;
    auto& loc = ruwa::i18n::CommandLocalization::instance();
    const QString lowerQuery = query.trimmed().toLower();

    for (const auto& [id, cmd] : m_commands) {
        int score = 0;

        // For hidden commands, check alias match first
        if (!cmd->showInPalette()) {
            QStringList aliasesToCheck = loc.aliases(QString::fromStdString(id));
            if (aliasesToCheck.isEmpty()) {
                aliasesToCheck = cmd->info().aliases;
            }

            for (const QString& alias : aliasesToCheck) {
                if (alias.toLower() == lowerQuery) {
                    score = 1000; // High score for exact alias match
                    break;
                }
            }

            if (score <= 0) {
                continue; // Skip hidden commands that don't match alias
            }
        } else {
            // For visible commands, use normal scoring
            score = calculateSearchScore(cmd.get(), query);
            if (score <= 0) {
                continue;
            }
        }

        scored.append({ cmd.get(), score });
    }

    // Sort by score (descending)
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    // Extract commands
    QList<Command*> result;
    result.reserve(qMin(maxResults, static_cast<int>(scored.size())));

    for (int i = 0; i < qMin(maxResults, static_cast<int>(scored.size())); ++i) {
        result.append(scored[i].first);
    }

    return result;
}

Command* CommandRegistry::findCommandByAlias(const QString& alias) const
{
    if (alias.isEmpty())
        return nullptr;

    const QString lowerAlias = alias.trimmed().toLower();
    auto& loc = ruwa::i18n::CommandLocalization::instance();

    for (const auto& [id, cmd] : m_commands) {
        QStringList aliasesToCheck = loc.aliases(QString::fromStdString(id));
        if (aliasesToCheck.isEmpty()) {
            aliasesToCheck = cmd->info().aliases;
        }
        for (const QString& a : aliasesToCheck) {
            if (a.trimmed().toLower() == lowerAlias) {
                return cmd.get();
            }
        }
    }
    return nullptr;
}

int CommandRegistry::calculateSearchScore(const Command* cmd, const QString& query) const
{
    const CommandInfo info = cmd->info();
    auto& loc = ruwa::i18n::CommandLocalization::instance();

    const QString effectiveTitle = loc.title(info.id).isEmpty() ? info.title : loc.title(info.id);
    const QString effectiveDesc
        = loc.description(info.id).isEmpty() ? info.description : loc.description(info.id);
    QStringList effectiveAliases = loc.aliases(info.id);
    if (effectiveAliases.isEmpty()) {
        effectiveAliases = info.aliases;
    }

    const QString lowerQuery = query.toLower();
    int score = 0;

    // Exact alias match (highest priority)
    for (const QString& alias : effectiveAliases) {
        if (alias.toLower() == lowerQuery) {
            return 1000;
        }
    }

    // ID exact match
    if (info.id.toLower() == lowerQuery) {
        return 900;
    }

    // Title starts with query
    if (effectiveTitle.toLower().startsWith(lowerQuery)) {
        score = qMax(score, 800);
    }

    // Alias starts with query
    for (const QString& alias : effectiveAliases) {
        if (alias.toLower().startsWith(lowerQuery)) {
            score = qMax(score, 700);
            break;
        }
    }

    // ID contains query
    if (info.id.toLower().contains(lowerQuery)) {
        score = qMax(score, 500);
    }

    // Title contains query
    if (effectiveTitle.toLower().contains(lowerQuery)) {
        score = qMax(score, 400);
    }

    // Alias contains query
    for (const QString& alias : effectiveAliases) {
        if (alias.toLower().contains(lowerQuery)) {
            score = qMax(score, 300);
            break;
        }
    }

    // Description contains query (lowest priority)
    if (effectiveDesc.toLower().contains(lowerQuery)) {
        score = qMax(score, 100);
    }

    // Fuzzy matching: check if all characters appear in order
    if (score == 0) {
        const QString lowerTitle = effectiveTitle.toLower().remove(' ');
        int queryIdx = 0;

        for (int i = 0; i < lowerTitle.length() && queryIdx < lowerQuery.length(); ++i) {
            if (lowerTitle[i] == lowerQuery[queryIdx]) {
                ++queryIdx;
            }
        }

        if (queryIdx == lowerQuery.length()) {
            score = 50; // All characters found in order
        }
    }

    return score;
}

} // namespace ruwa::core
