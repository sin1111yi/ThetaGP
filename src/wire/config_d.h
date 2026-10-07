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
// one at a time and listed so a host can ask which ones this firmware carries,
// plus the three arms that move the whole configuration through the profile it
// belongs to. Every key arm answers the key it reached, so a host pairing
// replies with requests sees which field moved.
class ConfigDomain {
public:
  // The value of the key the request names: a number for a scalar key, a run of
  // elements for an array key, or the refusal that says the key is one this
  // firmware does not expose.
  static void getKey(const ThetaGP_Request &request, ThetaGP_Reply &reply);

  // Writes the value the request carries into the key it names, held to the
  // range and the element count that key declares. The reply carries the key
  // and nothing of the value: a read is where the value is read back.
  static void setKey(const ThetaGP_Request &request, ThetaGP_Reply &reply);

  // One entry per key the control protocol accepts, in the table's order, each
  // with the range the key holds and whether a write reaches it only at the
  // next boot.
  static void listKeys(ThetaGP_Reply &reply);

  // Write the configuration in effect to the profile it belongs to. A board
  // with no storage chip answers persisted false rather than refusing: the
  // values are in effect, they just do not outlive the power cycle.
  static void save(ThetaGP_Reply &reply);

  // Read the profile the configuration belongs to back over it, starting from
  // the compiled-in defaults. The profile the configuration belongs to is not
  // changed by it.
  static void load(ThetaGP_Reply &reply);

  // Replace the configuration in effect with the compiled-in defaults, writing
  // nothing to persistent storage: writing it out is save's job.
  static void factoryReset(ThetaGP_Reply &reply);
};

} // namespace ThetaGP::Wire
