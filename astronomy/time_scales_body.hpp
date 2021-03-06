﻿
#pragma once

#include "astronomy/time_scales.hpp"

#include <array>
#include <cstdint>

#include "astronomy/date_time.hpp"
#include "astronomy/epoch.hpp"
#include "geometry/named_quantities.hpp"
#include "glog/logging.h"
#include "quantities/si.hpp"

namespace principia {
namespace astronomy {
namespace internal_time_scales {

using astronomy::date_time::Date;
using astronomy::date_time::DateTime;
using astronomy::date_time::operator""_Date;
using astronomy::date_time::operator""_DateTime;
using quantities::si::Day;
using quantities::si::Second;

// Returns the duration between 2000-01-01T12:00:00 and |date_time| (of the same
// timescale), not counting any leap seconds that may have occurred in the past.
// |date_time| itself may be leap second.
// Note that this may count non-SI seconds depending on the time scale according
// to which it is interpreted.
// On a time scale with leap seconds, this is not injective: a positive leap
// second and the following second map to the same interval.
constexpr quantities::Time TimeScale(DateTime const& date_time) {
  return (date_time.time().millisecond() / 1e3) * Second +
         (date_time.time().second() +
          60 * (date_time.time().minute() +
                60 * (date_time.time().hour() - 12 +
                      24 * static_cast<std::int64_t>(
                               date_time.date().mjd() -
                               "2000-01-01"_Date.mjd())))) * Second;
}

constexpr double mjd(quantities::Time const& from_j2000) {
  return from_j2000 / Day + 51544.5;
}

constexpr Instant FromTT(quantities::Time const& from_j2000) {
  return J2000 + from_j2000;
}

constexpr Instant FromTAI(quantities::Time const& tai) {
  return FromTT(tai + 32.184 * Second);
}

// Utilities for modern UTC (since 1972).

constexpr std::array<int, (2017 - 1972) * 2> leap_seconds = {{
    +1, +1,  // 1972
    +0, +1,  // 1973
    +0, +1,  // 1974
    +0, +1,  // 1975
    +0, +1,  // 1976
    +0, +1,  // 1977
    +0, +1,  // 1978
    +0, +1,  // 1979
    +0, +0,  // 1980
    +1, +0,  // 1981
    +1, +0,  // 1982
    +1, +0,  // 1983
    +0, +0,  // 1984
    +1, +0,  // 1985
    +0, +0,  // 1986
    +0, +1,  // 1987
    +0, +0,  // 1988
    +0, +1,  // 1989
    +0, +1,  // 1990
    +0, +0,  // 1991
    +1, +0,  // 1992
    +1, +0,  // 1993
    +1, +0,  // 1994
    +0, +1,  // 1995
    +0, +0,  // 1996
    +1, +0,  // 1997
    +0, +1,  // 1998
    +0, +0,  // 1999
    +0, +0,  // 2000
    +0, +0,  // 2001
    +0, +0,  // 2002
    +0, +0,  // 2003
    +0, +0,  // 2004
    +0, +1,  // 2005
    +0, +0,  // 2006
    +0, +0,  // 2007
    +0, +1,  // 2008
    +0, +0,  // 2009
    +0, +0,  // 2010
    +0, +0,  // 2011
    +1, +0,  // 2012
    +0, +0,  // 2013
    +0, +0,  // 2014
    +1, +0,  // 2015
    +0, +1,  // 2016
}};

// Returns +1 if a positive leap second was inserted at the end of the given
// |month| of the given |year|, 0 otherwise.
constexpr int LeapSecond(int const year, int const month) {
  return month == 6
             ? CHECKING((year - 1972) * 2 < leap_seconds.size(),
                        leap_seconds[(year - 1972) * 2])
             : CHECKING(month == 12,
                        CHECKING((year - 1972) * 2 + 1 < leap_seconds.size(),
                                 leap_seconds[(year - 1972) * 2 + 1]));
}

// Returns UTC - TAI on the given UTC day (similar to Bulletin C).
constexpr quantities::Time ModernUTCMinusTAI(Date const& utc_date) {
  return utc_date.month() == 1 && utc_date.day() == 1
             ? utc_date.year() == 1972
                   ? -10 * Second
                   : -LeapSecond(utc_date.year() - 1, 6) * Second +
                         -LeapSecond(utc_date.year() - 1, 12) * Second +
                         ModernUTCMinusTAI(
                             Date::Calendar(utc_date.year() - 1, 1, 1))
             : (utc_date.month() > 6 ? -LeapSecond(utc_date.year(), 6) * Second
                                     : 0 * Second) +
                   ModernUTCMinusTAI(Date::Calendar(utc_date.year(), 1, 1));
}

constexpr bool IsValidModernUTC(DateTime const& date_time) {
  return !date_time.time().is_leap_second() ||
         LeapSecond(date_time.date().year(), date_time.date().month()) == +1;
}

// Utilities for stretchy UTC (pre-1972).  This timescale includes rate changes
// as well as fractional second leaps.

// The (MJD - d) * t term from
// https://hpiers.obspm.fr/iers/bul/bulc/UTC-TAI.history.
constexpr quantities::Time RateTAIMinusStretchyUTC(DateTime const& utc) {
  return utc.date() < "1962-01-01"_Date
             ? (mjd(TimeScale(utc)) - 37'300) * 0.001'296 * Second
       : utc.date() < "1964-01-01"_Date
             ? (mjd(TimeScale(utc)) - 37'665) * 0.001'123'2 * Second
       : utc.date() < "1966-01-01"_Date
             ? (mjd(TimeScale(utc)) - 38'761) * 0.001'296 * Second
             : (mjd(TimeScale(utc)) - 39'126) * 0.002'592 * Second;
}

// The constant term.
constexpr quantities::Time OffsetTAIMinusStretchyUTC(Date const& utc_date) {
  return utc_date < "1961-08-01"_Date ? 1.422'818'0 * Second
       : utc_date < "1962-01-01"_Date ? 1.372'818'0 * Second
       : utc_date < "1963-11-01"_Date ? 1.845'858'0 * Second
       : utc_date < "1964-01-01"_Date ? 1.945'858'0 * Second
       : utc_date < "1964-04-01"_Date ? 3.240'130'0 * Second
       : utc_date < "1964-09-01"_Date ? 3.340'130'0 * Second
       : utc_date < "1965-01-01"_Date ? 3.440'130'0 * Second
       : utc_date < "1965-03-01"_Date ? 3.540'130'0 * Second
       : utc_date < "1965-07-01"_Date ? 3.640'130'0 * Second
       : utc_date < "1965-09-01"_Date ? 3.740'130'0 * Second
       : utc_date < "1966-01-01"_Date ? 3.840'130'0 * Second
       : utc_date < "1968-02-01"_Date ? 4.313'170'0 * Second
                                      : 4.213'170'0 * Second;
}

// Returns TAI - UTC at the given point on the UTC timescale.
constexpr quantities::Time TAIMinusStretchyUTC(DateTime const& utc) {
  return OffsetTAIMinusStretchyUTC(utc.date()) + RateTAIMinusStretchyUTC(utc);
}

// Returns |true| if |utc| is within a leap of the given number of
// |milliseconds| inserted before |next_day|.
constexpr bool IsValidPositiveStretchyUTCLeap(DateTime const& utc,
                                             Date const& next_day,
                                             double const& milliseconds) {
  return utc.time().is_leap_second() && utc.date().next_day() == next_day &&
         utc.time().millisecond() < milliseconds;
}

// If |utc| is on the day before |next_day|, returns true its time is consistent
// with a negative leap of the given number of |milliseconds| before |next_day|.
// If |utc| is not on the day before |next_day|, returns true.
constexpr bool IsValidStretchyUTCIfOnDayOfNegativeLeap(DateTime const& utc,
                                                       Date const& next_day,
                                                       int const milliseconds) {
  return CHECKING(milliseconds > 0,
                  utc.date().next_day() != next_day ||
                      utc.time().hour() < 23 ||
                      utc.time().minute() < 59 ||
                      utc.time().millisecond() < 1000 - milliseconds);
}

// A list of leaps is found at
// https://hpiers.obspm.fr/iers/bul/bulc/TimeSteps.history.
constexpr bool IsValidStretchyUTC(DateTime const& utc) {
  return utc.date().year() >= 1961 && utc.date().year() < 1972 &&
         IsValidStretchyUTCIfOnDayOfNegativeLeap(utc, "1961-08-01"_Date, 50) &&
         IsValidStretchyUTCIfOnDayOfNegativeLeap(utc, "1968-02-01"_Date, 100) &&
         (!utc.time().is_leap_second() ||
          IsValidPositiveStretchyUTCLeap(utc, "1963-11-01"_Date, 100) ||
          IsValidPositiveStretchyUTCLeap(utc, "1964-04-01"_Date, 100) ||
          IsValidPositiveStretchyUTCLeap(utc, "1964-09-01"_Date, 100) ||
          IsValidPositiveStretchyUTCLeap(utc, "1965-01-01"_Date, 100) ||
          IsValidPositiveStretchyUTCLeap(utc, "1965-03-01"_Date, 100) ||
          IsValidPositiveStretchyUTCLeap(utc, "1965-07-01"_Date, 100) ||
          IsValidPositiveStretchyUTCLeap(utc, "1965-09-01"_Date, 100) ||
          IsValidPositiveStretchyUTCLeap(utc, "1972-01-01"_Date, 107.7580));
}

// Utilities for UT1.

// An entry in the Experimental EOP C02 time series; represents UT1 - TAI at the
// given |ut1_mjd|.
struct ExperimentalEOPC02Entry final {
  constexpr ExperimentalEOPC02Entry(double ut1_mjd,
                                    quantities::Time ut1_minus_tai);

  double const ut1_mjd;
  quantities::Time const ut1_minus_tai;
};

constexpr ExperimentalEOPC02Entry::ExperimentalEOPC02Entry(
    double const ut1_mjd,
    quantities::Time const ut1_minus_tai)
    : ut1_mjd(ut1_mjd),
      ut1_minus_tai(ut1_minus_tai) {}

// An entry in the EOP (IERS) 08 C04 time series; represents UT1 - UTC at
// 00:00:00 on the given |utc_date|.  The date is given as an integer of the
// form YYYY'MM'DD, which is then interpreted on demand, in order to limit the
// number of constexpr steps.
struct EOPC04Entry final {
  constexpr EOPC04Entry(int utc_date, quantities::Time const& ut1_minus_utc);

  constexpr DateTime utc() const;
  constexpr quantities::Time ut1() const;
  constexpr quantities::Time ut1_minus_tai() const;

  int const utc_date;
  quantities::Time const ut1_minus_utc;
};

constexpr EOPC04Entry::EOPC04Entry(
    int const utc_date,
    quantities::Time const& ut1_minus_utc)
    : utc_date(utc_date),
      ut1_minus_utc(ut1_minus_utc) {}

constexpr DateTime EOPC04Entry::utc() const {
  return DateTime::BeginningOfDay(Date::YYYYMMDD(utc_date));
}

constexpr quantities::Time EOPC04Entry::ut1() const {
  return TimeScale(utc()) + ut1_minus_utc;
}

constexpr quantities::Time EOPC04Entry::ut1_minus_tai() const {
  return utc().date().year() >= 1972
             ? ut1_minus_utc + ModernUTCMinusTAI(utc().date())
             : ut1_minus_utc - TAIMinusStretchyUTC(utc());
}

// NOTE(egg): these have to be defined here, because they depend on internal
// classes defined above.
#include "astronomy/experimental_eop_c02.generated.h"
#include "astronomy/eop_c04.generated.h"

// Returns the last entry in [begin, begin + size[ whose UT1 is less than or
// equal to the given |ut1|.  The range [begin, begin + size[ must be sorted by
// UT1.
// We have to use |begin| and |size| rather than |begin| and |end| because
// otherwise MSVC complains about undefinedness of |end - begin| (even though
// Intellisense is fine with it).
constexpr ExperimentalEOPC02Entry const* LookupUT1(
    quantities::Time const& ut1,
    ExperimentalEOPC02Entry const* begin,
    std::ptrdiff_t const size) {
  return CHECKING(size > 0,
                  size == 1
                      ? CHECKING(begin->ut1_mjd <= mjd(ut1), begin)
                      : (begin + size / 2)->ut1_mjd <= mjd(ut1)
                            ? LookupUT1(ut1, begin + size / 2, size - size / 2)
                            : LookupUT1(ut1, begin, size / 2));
}

constexpr EOPC04Entry const* LookupUT1(quantities::Time const& ut1,
                                       EOPC04Entry const* begin,
                                       std::ptrdiff_t const size) {
  return CHECKING(size > 0,
                  size == 1
                      ? CHECKING(begin->ut1() <= ut1, begin)
                      : (begin + size / 2)->ut1() <= ut1
                            ? LookupUT1(ut1, begin + size / 2, size - size / 2)
                            : LookupUT1(ut1, begin, size / 2));
}

constexpr ExperimentalEOPC02Entry const* LookupInExperimentalEOPC02(
    quantities::Time const& ut1) {
  return LookupUT1(ut1, &experimental_eop_c02[0], experimental_eop_c02.size());
}

constexpr EOPC04Entry const* LookupInEOPC04(
    quantities::Time const& ut1) {
  return LookupUT1(ut1, &eop_c04[0], eop_c04.size());
}

// Linear interpolation on the UT1 range [low->ut1(), (low + 1)->ut1()].
constexpr Instant InterpolatedEOPC04(EOPC04Entry const* low,
                                     quantities::Time const& ut1) {
  // TODO(egg): figure out whether using the divided difference of the
  // |p->ut1_minus_tai()|s leads to less catastrophic cancellation than using
  // the divided difference of the |DateTimeAsUTC(p->utc())|s.
  return FromTAI(ut1 - (low->ut1_minus_tai() +
                        (ut1 - low->ut1()) * ((low + 1)->ut1_minus_tai() -
                                              low->ut1_minus_tai()) /
                            ((low + 1)->ut1() - low->ut1())));
}

// Linear interpolation on the UT1 range given by the range of MJDs
// [low->ut1_mjd, (low + 1)->ut1_mjd].
constexpr Instant InterpolatedExperimentalEOPC02(
    ExperimentalEOPC02Entry const* low,
    quantities::Time const& ut1) {
  return FromTAI(ut1 - (low->ut1_minus_tai +
                        (mjd(ut1) - low->ut1_mjd) *
                            ((low + 1)->ut1_minus_tai - low->ut1_minus_tai) /
                            ((low + 1)->ut1_mjd - low->ut1_mjd)));
}

// Linear interpolation in the segment between the UT1s |low.ut1_mjd| and
// |high.ut1()|, used to get continuity when switching between the series.
constexpr Instant ExperimentalEOPC02ToEOPC04(ExperimentalEOPC02Entry const& low,
                                             EOPC04Entry const& high,
                                             quantities::Time const& ut1) {
  return FromTAI(ut1 - (low.ut1_minus_tai +
                        (mjd(ut1) - low.ut1_mjd) *
                            (high.ut1_minus_tai() - low.ut1_minus_tai) /
                            ((mjd(high.ut1()) - low.ut1_mjd))));
}

constexpr Instant FromUT1(quantities::Time const ut1) {
  return ut1 < eop_c04.front().ut1()
             ? ((LookupInExperimentalEOPC02(ut1) + 1)->ut1_mjd >
                        mjd(eop_c04.front().ut1())
                    ? ExperimentalEOPC02ToEOPC04(
                          *LookupInExperimentalEOPC02(ut1),
                          eop_c04.front(),
                          ut1)
                    : InterpolatedExperimentalEOPC02(
                          LookupInExperimentalEOPC02(ut1), ut1))
             : InterpolatedEOPC04(LookupInEOPC04(ut1), ut1);
}

// Conversions from |DateTime| to |Instant|.

constexpr Instant DateTimeAsTT(DateTime const& tt) {
  return CHECKING(!tt.time().is_leap_second(), FromTT(TimeScale(tt)));
}

constexpr Instant DateTimeAsTAI(DateTime const& tai) {
  return CHECKING(!tai.time().is_leap_second(), FromTAI(TimeScale(tai)));
}

constexpr Instant DateTimeAsUTC(DateTime const& utc) {
  return utc.time().is_end_of_day()
             ? DateTimeAsUTC(utc.normalized_end_of_day())
             : utc.date().year() < 1972
                   ? CHECKING(
                         IsValidStretchyUTC(utc),
                         FromTAI(TimeScale(utc) + TAIMinusStretchyUTC(utc)))
                   : CHECKING(IsValidModernUTC(utc),
                              FromTAI(TimeScale(utc) -
                                      ModernUTCMinusTAI(utc.date())));
}

constexpr Instant DateTimeAsUT1(DateTime const& ut1) {
  return CHECKING(
      !ut1.time().is_leap_second() &&
          mjd(TimeScale(ut1)) >= experimental_eop_c02.front().ut1_mjd &&
          TimeScale(ut1) < eop_c04.back().ut1(),
      FromUT1(TimeScale(ut1)));
}

// |Instant| date literals.

#if (PRINCIPIA_COMPILER_CLANG || PRINCIPIA_COMPILER_CLANG_CL) && WE_LIKE_N3599
template<typename C, C... str>
constexpr std::array<C, sizeof...(str)> unpack_as_array() {
  return std::array<C, sizeof...(str)>{{str...}};
}

template<typename T>
constexpr T const& as_const_ref(T const& t) {
  return t;
}

template<typename C, std::size_t size>
constexpr C const* c_str(std::array<C, size> const& array) {
  // In C++17 this could be |data()|.  For the moment this does the job.
  return &as_const_ref(array)[0];
}

// NOTE(egg): In the following three functions, the |constexpr| intermediate
// variable forces failures to occur at compile time and not as glog |CHECK|
// failures at evaluation.

template<typename C, C... str>
constexpr Instant operator""_TAI() {
  constexpr auto result = DateTimeAsTAI(
      operator""_DateTime(c_str(unpack_as_array<C, str...>()), sizeof...(str)));
  return result;
}

template<typename C, C... str>
constexpr Instant operator""_TT() {
  constexpr auto result = DateTimeAsTT(
      operator""_DateTime(c_str(unpack_as_array<C, str...>()), sizeof...(str)));
  return result;
}

template<typename C, C... str>
constexpr Instant operator""_UTC() {
  constexpr auto result = DateTimeAsUTC(
      operator""_DateTime(c_str(unpack_as_array<C, str...>()), sizeof...(str)));
  return result;
}

template<typename C, C... str>
constexpr Instant operator""_UT1() {
  constexpr auto result = DateTimeAsUT1(
      operator""_DateTime(c_str(unpack_as_array<C, str...>()), sizeof...(str)));
  return result;
}
#else
constexpr Instant operator""_TAI(char const* str, std::size_t size) {
  return DateTimeAsTAI(operator""_DateTime(str, size));
}

constexpr Instant operator""_TT(char const* str, std::size_t size) {
  return DateTimeAsTT(operator""_DateTime(str, size));
}

constexpr Instant operator""_UTC(char const* str, std::size_t size) {
  return DateTimeAsUTC(operator""_DateTime(str, size));
}

constexpr Instant operator""_UT1(char const* str, std::size_t size) {
  return DateTimeAsUT1(operator""_DateTime(str, size));
}
#endif

}  // namespace internal_time_scales
}  // namespace astronomy
}  // namespace principia
