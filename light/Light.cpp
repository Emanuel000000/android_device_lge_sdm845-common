/*
 * Copyright (C) 2017-2018 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "light"

#include "Light.h"

#include <log/log.h>

namespace {
using android::hardware::light::V2_0::LightState;

static uint32_t rgbToBrightness(const LightState& state) {
    uint32_t color = state.color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff)) + (150 * ((color >> 8) & 0xff)) +
            (29 * (color & 0xff))) >> 8;
}

static bool isLit(const LightState& state) {
    return (state.color & 0x00ffffff);
}
} // anonymous namespace

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

Light::Light(std::ofstream&& backlight, std::ofstream&& emotionalBlinkPattern, std::ofstream&& emotionalOnOffPattern) :
    mBacklight(std::move(backlight)),
    mEmotionalBlinkPath(std::move(emotionalBlinkPattern)),
    mEmotionalOnOffPath(std::move(emotionalOnOffPattern)) {
    auto attnFn(std::bind(&Light::setAttentionLight, this, std::placeholders::_1));
    auto backlightFn(std::bind(&Light::setBacklight, this, std::placeholders::_1));
    auto batteryFn(std::bind(&Light::setBatteryLight, this, std::placeholders::_1));
    auto notifFn(std::bind(&Light::setNotificationLight, this, std::placeholders::_1));
    mLights.emplace(std::make_pair(Type::ATTENTION, attnFn));
    mLights.emplace(std::make_pair(Type::BACKLIGHT, backlightFn));
    mLights.emplace(std::make_pair(Type::BATTERY, batteryFn));
    mLights.emplace(std::make_pair(Type::NOTIFICATIONS, notifFn));
}

// Methods from ::android::hardware::light::V2_0::ILight follow.
Return<Status> Light::setLight(Type type, const LightState& state) {
    auto it = mLights.find(type);
    if (it == mLights.end()) {
        return Status::LIGHT_NOT_SUPPORTED;
    }
    it->second(state);
    return Status::SUCCESS;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    std::vector<Type> types;
    for (auto const& light : mLights) {
        types.push_back(light.first);
    }
    _hidl_cb(types);
    return Void();
}

void Light::setAttentionLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mAttentionState = state;
    checkLightStateLocked();
}

void Light::setBacklight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    uint32_t brightness = rgbToBrightness(state) * 16;
    mBacklight << brightness << std::endl;
}

void Light::setBatteryLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mBatteryState = state;
    checkLightStateLocked();
}

void Light::setNotificationLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mNotificationState = state;
    checkLightStateLocked();
}

void Light::checkLightStateLocked() {
    if (isLit(mNotificationState)) {
        setLightLocked(mNotificationState);
    } else if (isLit(mAttentionState)) {
        setLightLocked(mAttentionState);
    } else if (isLit(mBatteryState)) {
        setLightLocked(mBatteryState);
    } else {
        /* Lights off */
        mEmotionalBlinkPath << "0x0,-1,-1" << std::endl;
        mEmotionalOnOffPath << "0x0" << std::endl;
    }
}

void Light::setLightLocked(const LightState& state) {
    int onMS, offMS;
    uint32_t color;
    char pattern[PAGE_SIZE];

    switch (state.flashMode) {
        case Flash::TIMED:
            onMS = state.flashOnMs;
            offMS = state.flashOffMs;
            break;
        case Flash::NONE:
            onMS = 0;
            offMS = 0;
            break;
        default:
            onMS = -1;
            offMS = -1;
            break;
    }

    color = state.color & 0x00ffffff;

    if (offMS <= 0) {
        sprintf(pattern,"0x%06x", color);
        ALOGD("%s: Using onoff pattern: inColor=0x%06x\n", __func__, color);
        mEmotionalOnOffPath << pattern << std::endl;
    } else {
        sprintf(pattern,"0x%06x,%d,%d", color, onMS, offMS);
        ALOGD("%s: Using blink pattern: inColor=0x%06x delay_on=%d, delay_off=%d\n",
              __func__, color, onMS, offMS);
        mEmotionalBlinkPath << pattern << std::endl;
    }
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
