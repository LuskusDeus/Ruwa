// SPDX-License-Identifier: MPL-2.0

#include "EmptyStateTab.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/FontFamilyNames.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QLabel>

namespace ruwa::ui::tabs {

namespace {

/**
 * @brief Renders ASCII art using vector paths to avoid subpixel artifacts
 */
class AsciiArtWidget : public QWidget {
public:
    explicit AsciiArtWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        QFont f(ruwa::ui::core::FontFamilyNames::Consolas);
        f.setStyleHint(QFont::Monospace);
        f.setPointSize(7);
        f.setFixedPitch(true);
        f.setStyleStrategy(QFont::NoSubpixelAntialias);
        setFont(f);

        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setTextColor(const QColor& color)
    {
        if (m_color != color) {
            m_color = color;
            update();
        }
    }

    QSize sizeHint() const override
    {
        if (m_cachedSize.isValid())
            return m_cachedSize;

        QFontMetrics fm(font());
        int lineHeight = fm.height();
        int width = 0;
        int lineCount = 0;

        for (const auto& line : m_lines) {
            int w = fm.horizontalAdvance(line);
            if (w > width)
                width = w;
            lineCount++;
        }

        m_cachedSize = QSize(width, lineCount * lineHeight);
        return m_cachedSize;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QFontMetrics fm(font());
        int lineHeight = fm.height();
        int y = fm.ascent();

        QPainterPath path;

        for (const QString& line : m_lines) {
            path.addText(0, y, font(), line);
            y += lineHeight;
        }

        painter.fillPath(path, m_color);
    }

private:
    QColor m_color = Qt::white;
    mutable QSize m_cachedSize;

    const QStringList m_lines = QString::fromUtf8(R"(
 \`*-. 
  )  _`-. 
 .  : `. . 
 : _   '  \ 
 ; o` _.   `*-._ 
 `-.-'          `-. 
   ;       `       `. 
   :.       .        \ 
   . \  .   :   .-'   . 
   '  `+.;  ;  '      : 
   :  '  |    ;       ;-. 
   ; '   : :`-:     _.`* ;
  /  .*' ; .*`- +'  `*' 
 *-*   `*-*  `*-*'
)")
                                    .trimmed()
                                    .split('\n');
};

} // anonymous namespace

EmptyStateTab::EmptyStateTab(QWidget* parent)
    : ruwa::core::BaseTab(parent)
{
}

void EmptyStateTab::onInitialize()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(32);
    mainLayout->setAlignment(Qt::AlignCenter);

    m_asciiWidget = new AsciiArtWidget(this);
    static_cast<AsciiArtWidget*>(m_asciiWidget)->setTextColor(colors.text);
    mainLayout->addWidget(m_asciiWidget, 0, Qt::AlignHCenter);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setMaximumWidth(600);
    m_hintLabel->setText(tr("Meow-meow, it seems you closed all tabs. "
                            "Click on the Ruwa logo in the corner to open the home tab."));
    m_hintLabel->setStyleSheet(QString("color: %1; font-size: 13px;").arg(colors.textMuted.name()));
    mainLayout->addWidget(m_hintLabel, 0, Qt::AlignHCenter);

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() {
            const auto& c = ruwa::ui::core::ThemeManager::instance().colors();
            if (m_asciiWidget) {
                static_cast<AsciiArtWidget*>(m_asciiWidget)->setTextColor(c.text);
            }
            if (m_hintLabel) {
                m_hintLabel->setStyleSheet(
                    QString("color: %1; font-size: 13px;").arg(c.textMuted.name()));
            }
            update();
        });

    setBackgroundRole(QPalette::Window);
    setAutoFillBackground(true);
}

void EmptyStateTab::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    QPainter p(this);
    p.fillRect(rect(), colors.background);
}

} // namespace ruwa::ui::tabs
