#include <config.h>
#include "misc.h"
#include "Time.h"

/* Note for negative intervals:
 *
 * The formula for getting the value of a Time or timeval in seconds is:
 * sec + (ms * 0.001)
 *
 * This means -5.25 is { -6, 750 }.
 *
 * This almost never matters.  Negative intervals are almost never
 * displayed; they usually only exist as a result of math.  If you actually
 * need to do that, do:
 * printf("%-3.3f", tm.ms() / 1000.);
 */
void Time::set_now()
{
   xgettimeofday(&tsec, &tms);
   tms /= 1000; /* us -> ms */
}

void Time::set(int sec, int ms)
{
   tsec = sec;
   tms = ms;

   /* normalize to -1000 < tms < 1000 */
   if(tms < 0 || tms >= 1000) {
      tsec += tms / 1000;
      tms %= 1000;
   }
   /* 0 < tms < 1000 */
   if(tms < 0) {
      tms += 1000;
      tsec--;
   }
}

Time &Time::operator += (Time rhs)
{
   tsec += rhs.tsec;
   tms += rhs.tms;
   if (tms >= 1000) {
	 ++tsec;
	 tms -= 1000;
   }

   return *this;
}

Time Time::operator + (Time rhs) const
{
   Time ret(*this);
   ret += rhs;
   return ret;
}

Time &Time::operator -= (Time rhs)
{
   tsec -= rhs.tsec;
   tms -= rhs.tms;
   if (tms < 0) {
	 --tsec;
	 tms += 1000;
   }

   return *this;
}

Time Time::operator - (Time rhs) const
{
   Time ret(*this);
   ret -= rhs;
   return ret;
}

bool Time::operator < (Time cmp) const
{
   if(tsec == cmp.tsec)
      return tms < cmp.tms;
   else
      return tsec < cmp.tsec;
}

int Time::ms() const
{
   // don't allow overflows.
   if(tsec<-1000000)
      return -1000000000;
   if(tsec>1000000)
      return 1000000000;
   return (tsec * 1000) + tms;
}

/* don't want to add a dependancy to SMTask */
void Timer::reset()
{
   last.set_now();
}

void Timer::force()
{
   last.clear();
}

Timer::Timer()
{
   force();
}

int Timer::remaining() const
{
   Time now;
   now.set_now();

   Time next(last);
   next+=interval;
   next-=now;
   return next.ms();
}

bool Timer::go()
{
   int rem = remaining();

   if(rem <= 0) {
      reset();
      return true;
   }
   return false;
}
