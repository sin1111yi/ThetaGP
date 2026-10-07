/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "pb/ThetaGP.pb.h"

namespace ThetaGP::Wire {

// The configuration domain: the keys the key table exposes, read and written
// one at a time. Every arm answers the key it reached, so a host pairing
// replies with requests sees which field moved.
class ConfigDomain {
public:
  // The value of the key the request names, as a number, or the refusal that
  // says the key is unknown or holds a run this arm does not carry.
  static void getKey(const ThetaGP_Request &request, ThetaGP_Reply &reply);

  // Writes the value the request carries into the key it names, held to the
  // range that key declares. The reply carries the key and nothing of the
  // value: what a read answers is where the value is read back.
  static void setKey(const ThetaGP_Request &request, ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Wire
