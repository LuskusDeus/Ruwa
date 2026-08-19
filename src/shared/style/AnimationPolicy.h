// SPDX-License-Identifier: MPL-2.0

// AnimationPolicy.h
#ifndef RUWA_UI_CORE_STYLE_ANIMATIONPOLICY_H
#define RUWA_UI_CORE_STYLE_ANIMATIONPOLICY_H

#include <QAbstractAnimation>

namespace ruwa::ui::core::anim {

/**
 * @brief Whether UI animations should play at all.
 *
 * Single source of truth for the whole application. It currently reads the
 * master switch on WidgetStyleManager; the theme drives that switch, so call
 * sites never need to know where the value came from.
 *
 * Ambient, continuously looping motion (selection marching ants, the canvas
 * corner effect, the message popup border glow) is deliberately NOT governed by
 * this flag — it keeps running either way.
 */
bool enabled();

/**
 * @brief Playback speed multiplier, 0.5 (half speed) to 2.0 (double speed).
 *
 * 1.0 is the speed the animation was authored at. Meaningless while enabled()
 * is false.
 */
qreal speed();

/**
 * @brief Scale an authored duration by the current policy.
 *
 * Returns 0 when animations are disabled, and otherwise the authored duration
 * divided by the speed multiplier — faster playback means a shorter animation.
 * Call it where the animation is started rather than where it is constructed,
 * so a policy change takes effect without rebuilding the widget.
 */
int duration(int authoredMs);

/**
 * @brief Run @p animation to completion at once.
 *
 * Applies the animation's end value and delivers finished() synchronously,
 * before this call returns, so completion handlers (hide(), deleteLater(),
 * state changes, emitted signals) run exactly as they would after a real
 * animation. This is what makes turning animations off safe: no call site has
 * to duplicate the logic that lives in its finished() handler, and none of it
 * is left to a zero-duration animation's timing.
 *
 * An endlessly looping animation has no end value to jump to; such an animation
 * is started normally instead.
 *
 * @param animation May be null, in which case this is a no-op.
 * @param policy Passed through to QAbstractAnimation::start().
 */
void finishNow(QAbstractAnimation* animation,
    QAbstractAnimation::DeletionPolicy policy = QAbstractAnimation::KeepWhenStopped);

/**
 * @brief Start @p animation, honouring the animation policy.
 *
 * Plays it normally when animations are enabled, and finishes it at once via
 * finishNow() when they are not. Prefer this over a bare start() anywhere the
 * animation's finished() handler carries meaning.
 */
void start(QAbstractAnimation* animation,
    QAbstractAnimation::DeletionPolicy policy = QAbstractAnimation::KeepWhenStopped);

} // namespace ruwa::ui::core::anim

#endif // RUWA_UI_CORE_STYLE_ANIMATIONPOLICY_H
