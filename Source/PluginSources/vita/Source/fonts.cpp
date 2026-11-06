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

#include "fonts.h"

Fonts::Fonts() :
    proportional_regular_(Typeface::createSystemTypefaceFor(
        BinaryData::LatoRegular_ttf, BinaryData::LatoRegular_ttfSize)),
    proportional_light_(Typeface::createSystemTypefaceFor(
        BinaryData::LatoLight_ttf, BinaryData::LatoLight_ttfSize)),
    proportional_title_(Typeface::createSystemTypefaceFor(
        BinaryData::MontserratLight_otf, BinaryData::MontserratLight_otfSize)),
    proportional_title_regular_(Typeface::createSystemTypefaceFor(
        BinaryData::MontserratRegular_ttf, BinaryData::MontserratRegular_ttfSize)),
    monospace_(Typeface::createSystemTypefaceFor(
        BinaryData::DroidSansMono_ttf, BinaryData::DroidSansMono_ttfSize)) {

  // warm the typefaces up (Vital measured a test string with each)
  for (const Font* font : { &proportional_regular_, &proportional_light_, &proportional_title_, &monospace_ })
    GlyphArrangement::getStringWidth(*font, "test");
}
