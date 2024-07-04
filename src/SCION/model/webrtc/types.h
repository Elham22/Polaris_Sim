/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2024 ETH Zuerich
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Patrick Wicki <patrick.wicki@inf.ethz.ch>
 */

#ifndef WEBRTC_TYPES_H
#define WEBRTC_TYPES_H

#include "src/core/model/simulator.h"
#include <vector>
#include <algorithm>

namespace ns3 {

class DataSize;
class TimeDelta;

/**
 * BitRate class, with a similar interface to the DataRate class used in WebRTC code
 * ns-3 already has a DataRate class, but that one doesn't have enough functionality and
 * has quite a different interface.
 */
class BitRate
{
private:
  int64_t bps_; // bits per second

public:
  BitRate () : bps_ (0) {}
  explicit BitRate (int64_t rate) : bps_ (rate) {}
  BitRate (const BitRate &other) : bps_ (other.bps_) {}

  BitRate &operator= (const BitRate &other)
  {
    bps_ = other.bps_;
    return *this;
  }
  ~BitRate () {}

  static BitRate Zero () { return BitRate (0); }
  static BitRate Infinity () { return BitRate (std::numeric_limits<int64_t>::max ()); }
  static BitRate PlusInfinity () { return BitRate (std::numeric_limits<int64_t>::max ()); }
  static BitRate MinusInfinity () { return BitRate (std::numeric_limits<int64_t>::min ()); }
  static BitRate BitsPerSec (int64_t rate) { return BitRate (rate); }
  static BitRate KilobitsPerSec (int64_t rate) { return BitRate (rate * 1'000); }
  static BitRate BytesPerSec (int64_t rate) { return BitRate (rate * 8); }

  bool IsZero () const { return bps_ == 0; }
  bool
  IsFinite () const
  {
    return bps_ != std::numeric_limits<int64_t>::max () &&
           bps_ != std::numeric_limits<int64_t>::min ();
  }

  int64_t bps() const { return bps_; }
  double kbps() const { return bps_ / 1'000.0; }
  double mbps() const { return bps_ / 1'000'000.0; } 
  int64_t Bps () const { return bps_ / 8; }

  template<typename T>
  T bps() const {
    return static_cast<T>(bps_);
  }

  template<typename T>
  T kbps() const {
    return static_cast<T>(bps_ / 1'000.0);
  }

  friend std::ostream& operator<<(std::ostream& os, const BitRate& br) {
    if (br.bps_ < 1'000) {
      os << br.bps_ << " bps";
    } else if (br.bps_ < 1'000'000) {
      os << br.kbps() << " kbps";
    } else {
      os << br.bps_ / 1'000'000.0 << " Mbps";
    }
    return os;
  }

  BitRate operator+ (const BitRate &other) const { return BitRate (bps_ + other.bps_); }
  BitRate operator- (const BitRate &other) const { return BitRate (bps_ - other.bps_); }
  BitRate operator* (const BitRate &other) const { return BitRate (bps_ * other.bps_); }
  BitRate operator/ (const BitRate &other) const { return BitRate (bps_ / other.bps_); }

  DataSize operator*(const TimeDelta& delta) const;

  BitRate& operator-= (const BitRate &other) { bps_ -= other.bps_; return *this; }
  BitRate& operator+= (const BitRate &other) { bps_ += other.bps_; return *this; }

  // Template operators to work with any numeric type
  template<typename T>
  BitRate operator* (T factor) const {
    static_assert(std::is_arithmetic<T>::value, "Factor must be a numeric type");
    return BitRate(bps_ * static_cast<double>(factor));
  }

  // Template operator/ to work with any numeric type
  template<typename T>
  BitRate operator/ (T divisor) const {
    static_assert(std::is_arithmetic<T>::value, "Divisor must be a numeric type");
    if (divisor == 0) {
      throw std::invalid_argument("Division by zero");
    }
    return BitRate(bps_ / static_cast<double>(divisor));
  }

  // Double cast
  operator double () const { return bps_; }

  // Allow double as LHS for multiplication
  friend BitRate operator* (double factor, const BitRate &rate) { return rate * factor; }

  bool operator== (const BitRate &other) const { return bps_ == other.bps_; }
  bool operator!= (const BitRate &other) const { return bps_ != other.bps_; }
  bool operator< (const BitRate &other) const { return bps_ < other.bps_; }
  bool operator> (const BitRate &other) const { return bps_ > other.bps_; }
  bool operator<= (const BitRate &other) const { return bps_ <= other.bps_; }
  bool operator>= (const BitRate &other) const { return bps_ >= other.bps_; }
};

/**
 * DataSize class with same interfaces as the one used in WebRTC code
 */
class DataSize
{
private:
  int64_t bits_;

public:
  DataSize () : bits_ (0) {}
  explicit DataSize (int64_t size) : bits_ (size) {}
  DataSize (const DataSize &other) : bits_ (other.bits_) {}
  DataSize &operator= (const DataSize &other)
  {
    bits_ = other.bits_;
    return *this;
  }
  ~DataSize () {}

  static DataSize Zero () { return DataSize (0); }
  static DataSize Infinity () { return DataSize (std::numeric_limits<int64_t>::max ()); }
  static DataSize Bytes (int64_t bytes) { return DataSize (bytes * 8); }

  int64_t bytes () const { return bits_ / 8; }

  DataSize operator+ (const DataSize &other) const { return DataSize (bits_ + other.bits_); }
  DataSize operator- (const DataSize &other) const { return DataSize (bits_ - other.bits_); }
  DataSize operator* (const DataSize &other) const { return DataSize (bits_ * other.bits_); }
  double operator/ (const DataSize &other) const { return bits_ / other.bits_; }
  DataSize operator* (double factor) const { return DataSize (bits_ * factor); }
  DataSize operator/ (double factor) const { return DataSize (bits_ / factor); }
  BitRate operator/ (const TimeDelta &delta) const;
  TimeDelta operator/ (const BitRate &rate) const;

  bool operator== (const DataSize &other) const { return bits_ == other.bits_; }
  bool operator!= (const DataSize &other) const { return bits_ != other.bits_; }
  bool operator< (const DataSize &other) const { return bits_ < other.bits_; }
  bool operator> (const DataSize &other) const { return bits_ > other.bits_; }
  bool operator<= (const DataSize &other) const { return bits_ <= other.bits_; }
  bool operator>= (const DataSize &other) const { return bits_ >= other.bits_; }
};

/**
 * Class TimeDelta that internally uses ns-3 Time class but provides the same interface as the one used in WebRTC code
 */
class TimeDelta
{
private:
  Time time_;

public:
  TimeDelta () : time_ (Time (0)) {}
  explicit TimeDelta (Time time) : time_ (time) {}
  TimeDelta (const TimeDelta &other) : time_ (other.time_) {}
  TimeDelta &operator= (const TimeDelta &other)
  {
    time_ = other.time_;
    return *this;
  }
  ~TimeDelta () {}

  double seconds() const { return time_.GetDouble(); }

  int64_t ms() const { return time_.GetMilliSeconds(); }

  template<typename T>
  T seconds() const {
    return time_.GetSeconds(); // HACK
  }

  template<typename T>
  T ms() const {
    return time_.GetMilliSeconds();
  }

  static TimeDelta PlusInfinity () { return TimeDelta (Time::Max ()); }
  static TimeDelta MinusInfinity () { return TimeDelta (Time::Min ()); }
  static TimeDelta Millis (int64_t ms) { return TimeDelta (MilliSeconds (ms)); }
  static TimeDelta Seconds (double s) { return TimeDelta (Time::FromDouble(s, Time::Unit::S)); }
  static TimeDelta Zero () { return TimeDelta (Time (0)); }

  Time GetTime () const { return time_; }
  bool IsFinite () const { return time_ != Time::Max () && time_ != Time::Min (); }
  bool IsZero () const { return time_ == Time (0); }
  TimeDelta Clamped (TimeDelta min, TimeDelta max) const { return TimeDelta(std::clamp (time_, min.time_, max.time_)); }

  TimeDelta operator+ (const TimeDelta &other) const { return TimeDelta (time_ + other.time_); }
  TimeDelta operator- (const TimeDelta &other) const { return TimeDelta (time_ - other.time_); }
  double operator/ (const TimeDelta &other) const  { return (time_ / other.time_).GetDouble (); }
  TimeDelta operator* (double factor) const { return TimeDelta (time_ * factor); }

  template<typename T>
  TimeDelta operator/ (T divisor) const {
    static_assert(std::is_arithmetic<T>::value, "Divisor must be a numeric type");
    if (divisor == 0) {
      throw std::invalid_argument("Division by zero");
    }
    return TimeDelta(time_ / static_cast<double>(divisor));
  }

  friend TimeDelta operator* (double factor, const TimeDelta &delta) { return delta * factor; }

  bool operator== (const TimeDelta &other) const { return time_ == other.time_; }
  bool operator!= (const TimeDelta &other) const { return time_ != other.time_; }
  bool operator< (const TimeDelta &other) const { return time_ < other.time_; }
  bool operator> (const TimeDelta &other) const { return time_ > other.time_; }
  bool operator<= (const TimeDelta &other) const { return time_ <= other.time_; }
  bool operator>= (const TimeDelta &other) const { return time_ >= other.time_; }
};

/**
 * Class TimeStamp that internally uses ns-3 Time class but provides the same interface as the one used in WebRTC code
 */
class Timestamp
{
private:
  Time time_;

public:
  Timestamp () : time_ (Time (0)) {}
  Timestamp (Time time) : time_ (time) {}
  Timestamp (const Timestamp &other) : time_ (other.time_) {}
  Timestamp &operator= (const Timestamp &other)
  {
    time_ = other.time_;
    return *this;
  }
  ~Timestamp () {}

  double ms() const { return time_.GetDouble() * 1'000; }

  static Timestamp PlusInfinity () { return Timestamp (Time::Max ()); }
  static Timestamp MinusInfinity () { return Timestamp (Time::Min ()); }

  Time GetTime () const { return time_; }
  bool IsInfinite () const { return time_ == Time::Max () || time_ == Time::Min (); }
  bool IsFinite () const { return time_ != Time::Max () && time_ != Time::Min (); }
  bool IsPlusInfinity () const { return time_ == Time::Max (); }

  TimeDelta operator+ (const Timestamp &other) const { return TimeDelta (time_ + other.time_); }
  TimeDelta operator- (const Timestamp &other) const { return TimeDelta (time_ - other.time_); }
  Timestamp operator+ (const TimeDelta &delta) const { return Timestamp (time_ + delta.GetTime ()); }
  Timestamp operator- (const TimeDelta &delta) const { return Timestamp (time_ - delta.GetTime ()); }

  bool operator== (const Timestamp &other) const { return time_ == other.time_; }
  bool operator!= (const Timestamp &other) const { return time_ != other.time_; }
  bool operator< (const Timestamp &other) const { return time_ < other.time_; }
  bool operator> (const Timestamp &other) const { return time_ > other.time_; }
  bool operator<= (const Timestamp &other) const { return time_ <= other.time_; }
  bool operator>= (const Timestamp &other) const { return time_ >= other.time_; }
};

} // namespace ns3

#endif // WEBRTC_TYPES_H