#include <config.h>
#include "misc.h"
#include "Time.h"

void Time::set_now()
{
   xgettimeofday(&tsec, &tms);
   tms /= 1000; /* us -> ms */
}

Time Time::operator - (Time rhs) const
{
   Time ret(*this);
   ret -= rhs;
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

Time Time::operator + (int ms) const
{
   Time ret(*this);
   ret += ms;
   return ret;
}

Time &Time::operator += (int ms)
{
   tms += ms;
   tsec += tms / 1000;
   tms %= 1000;

   return *this;
}

bool Time::operator < (Time cmp) const
{
   if(tsec == cmp.tsec) return tms < cmp.tms;
   else return tsec < cmp.tsec;
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

int Timer::go(int ms)
{
   Time now;
   now.set_now();

   Time next = last + ms;

   if(now < next) {
      Time left = next - now;
      return left.ms();
   }

   reset();

   return 0;
}
