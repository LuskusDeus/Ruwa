// SPDX-License-Identifier: MPL-2.0

// AnimationPolicy.cpp
#include "AnimationPolicy.h"

#include "WidgetStyleManager.h"

namespace ruwa::ui::core::anim {

bool enabled()
{
    return WidgetStyleManager::instance().animationsEnabled();
}

bool canvasEnabled()
{
    return WidgetStyleManager::instance().canvasAnimationsEnabled();
}

qreal speed()
{
    return WidgetStyleManager::instance().animationSpeed();
}

int duration(int authoredMs)
{
    return WidgetStyleManager::instance().scaledDuration(authoredMs);
}

void finishNow(QAbstractAnimation* animation, QAbstractAnimation::DeletionPolicy policy)
{
    if (!animation) {
        return;
    }

    // Read both before starting: with DeleteWhenStopped the animation is handed
    // to deleteLater() the moment it reaches its end, which happens inside the
    // setCurrentTime() call below.
    const QAbstractAnimation::Direction direction = animation->direction();
    const int totalDuration = animation->totalDuration();

    if (totalDuration < 0) {
        // Endless loop — there is no end value to settle on.
        animation->start(policy);
        return;
    }

    // Starting puts the animation in the Running state; seeking to its end then
    // makes it stop the way a completed animation does, which is what emits
    // finished(). A zero-duration animation may already have completed inside
    // start(), and seeking a stopped animation is a harmless no-op, so this
    // stays correct either way.
    animation->start(policy);
    animation->setCurrentTime(direction == QAbstractAnimation::Backward ? 0 : totalDuration);
}

void start(QAbstractAnimation* animation, QAbstractAnimation::DeletionPolicy policy)
{
    if (!animation) {
        return;
    }

    if (enabled()) {
        animation->start(policy);
        return;
    }

    finishNow(animation, policy);
}

} // namespace ruwa::ui::core::anim
