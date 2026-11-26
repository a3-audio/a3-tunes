// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Internal, for tests only: lets a test drive a complete temporary file into the verification.
#pragma once

namespace a3t::testing
{
    // True in production: a lossy original whose copy would lose its encoder delay is refused
    // before anything is written. Off, the same file is written and the verification catches it.
    extern bool refuseLossyCopyUpFront;

    struct UpFrontLossyCheckOff
    {
        UpFrontLossyCheckOff() { refuseLossyCopyUpFront = false; }
        ~UpFrontLossyCheckOff() { refuseLossyCopyUpFront = true; }
    };
}
