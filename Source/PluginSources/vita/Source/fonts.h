/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "JuceHeader.h"

class Fonts {
  public:
    virtual ~Fonts() { }

    Font& proportional_regular() { return proportional_regular_; }
    Font& proportional_light() { return proportional_light_; }
    Font& proportional_title() { return proportional_title_; }
    Font& proportional_title_regular() { return proportional_title_regular_; }
    Font& monospace() { return monospace_; }

    // (Aquanode: let go when JUCE shuts down, not when the DLL unloads -
    // fonts and looks hold DirectWrite / Direct2D objects on Windows)
    static Fonts* instance() {
      struct Holder : public DeletedAtShutdown {
        Fonts object;
        ~Holder() override { slot() = nullptr; }
        static Holder*& slot() { static Holder* h = nullptr; return h; }
      };
      auto*& h = Holder::slot();
      if (h == nullptr)
        h = new Holder();
      return &h->object;
    }

  private:
    Fonts();

    Font proportional_regular_;
    Font proportional_light_;
    Font proportional_title_;
    Font proportional_title_regular_;
    Font monospace_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Fonts)
};

