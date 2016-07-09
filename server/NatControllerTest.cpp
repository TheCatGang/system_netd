/*
 * Copyright 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * NatControllerTest.cpp - unit tests for NatController.cpp
 */

#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "NatController.h"
#include "IptablesBaseTest.h"

using android::base::StringPrintf;

class NatControllerTest : public IptablesBaseTest {
public:
    NatControllerTest() {
        NatController::execFunction = fake_android_fork_exec;
    }

protected:
    NatController mNatCtrl;

    int setDefaults() {
        return mNatCtrl.setDefaults();
    }

    const ExpectedIptablesCommands FLUSH_COMMANDS = {
        { V4, "-F natctrl_FORWARD" },
        { V4, "-A natctrl_FORWARD -j DROP" },
        { V4, "-t nat -F natctrl_nat_POSTROUTING" },
    };

    const ExpectedIptablesCommands SETUP_COMMANDS = {
        { V4, "-F natctrl_FORWARD" },
        { V4, "-A natctrl_FORWARD -j DROP" },
        { V4, "-t nat -F natctrl_nat_POSTROUTING" },
        { V4, "-F natctrl_tether_counters" },
        { V4, "-X natctrl_tether_counters" },
        { V4, "-N natctrl_tether_counters" },
        { V4, "-t mangle -A natctrl_mangle_FORWARD -p tcp --tcp-flags SYN SYN "
              "-j TCPMSS --clamp-mss-to-pmtu" },
    };

    const ExpectedIptablesCommands TWIDDLE_COMMANDS = {
        { V4, "-D natctrl_FORWARD -j DROP" },
        { V4, "-A natctrl_FORWARD -j DROP" },
    };

    ExpectedIptablesCommands enableMasqueradeCommand(const char *extIf) {
        return {
            { V4, StringPrintf("-t nat -A natctrl_nat_POSTROUTING -o %s -j MASQUERADE", extIf) },
        };
    }

    ExpectedIptablesCommands startNatCommands(const char *intIf, const char *extIf) {
        return {
            { V4, StringPrintf("-A natctrl_FORWARD -i %s -o %s -m state --state"
                               " ESTABLISHED,RELATED -g natctrl_tether_counters", extIf, intIf) },
            { V4, StringPrintf("-A natctrl_FORWARD -i %s -o %s -m state --state INVALID -j DROP",
                               intIf, extIf) },
            { V4, StringPrintf("-A natctrl_FORWARD -i %s -o %s -g natctrl_tether_counters",
                               intIf, extIf) },
            { V4, StringPrintf("-A natctrl_tether_counters -i %s -o %s -j RETURN", intIf, extIf) },
            { V4, StringPrintf("-A natctrl_tether_counters -i %s -o %s -j RETURN", extIf, intIf) },
        };
    }

    ExpectedIptablesCommands stopNatCommands(const char *intIf, const char *extIf) {
        return {
            { V4, StringPrintf("-D natctrl_FORWARD -i %s -o %s -m state --state"
                               " ESTABLISHED,RELATED -g natctrl_tether_counters", extIf, intIf) },
            { V4, StringPrintf("-D natctrl_FORWARD -i %s -o %s -m state --state INVALID -j DROP",
                               intIf, extIf) },
            { V4, StringPrintf("-D natctrl_FORWARD -i %s -o %s -g natctrl_tether_counters",
                               intIf, extIf) },
        };
    }
};

TEST_F(NatControllerTest, TestSetupIptablesHooks) {
    mNatCtrl.setupIptablesHooks();
    expectIptablesCommands(SETUP_COMMANDS);
}

TEST_F(NatControllerTest, TestSetDefaults) {
    setDefaults();
    expectIptablesCommands(FLUSH_COMMANDS);
}

TEST_F(NatControllerTest, TestAddAndRemoveNat) {

    std::vector<ExpectedIptablesCommands> startFirstNat = {
        enableMasqueradeCommand("rmnet0"),
        startNatCommands("wlan0", "rmnet0"),
        TWIDDLE_COMMANDS,
    };
    mNatCtrl.enableNat("wlan0", "rmnet0");
    expectIptablesCommands(startFirstNat);

    std::vector<ExpectedIptablesCommands> startOtherNat = {
         startNatCommands("usb0", "rmnet0"),
         TWIDDLE_COMMANDS,
    };
    mNatCtrl.enableNat("usb0", "rmnet0");
    expectIptablesCommands(startOtherNat);

    ExpectedIptablesCommands stopOtherNat = stopNatCommands("wlan0", "rmnet0");
    mNatCtrl.disableNat("wlan0", "rmnet0");
    expectIptablesCommands(stopOtherNat);

    std::vector<ExpectedIptablesCommands> stopLastNat = {
        stopNatCommands("usb0", "rmnet0"),
        FLUSH_COMMANDS,
    };
    mNatCtrl.disableNat("usb0", "rmnet0");
    expectIptablesCommands(stopLastNat);
}
