#ifndef TIME_CLASSES_H
#define TIME_CLASSES_H

class Time {
   time_t tsec;
   int tms;

public:
   /* set the time to now */
   void set_now();

   /* set time to sec,ms */
   void set(int sec, int ms);

   /* clear to the beginning of time */
   void clear() { set(0,0); }

   int sec() const { return tsec; }
   int ms() const;

   Time operator + (Time rhs) const;
   Time &operator += (Time rhs);
   Time operator - (Time rhs) const;
   Time &operator -= (Time rhs);

   bool operator < (Time cmp) const;
   bool operator >= (Time cmp) const { return !(*this < cmp); }

   Time() { clear(); }
   Time(int ms) { set(0, ms); }
};

class Timer {
   /* last time this timer occured */
   Time last;

   /* interval between calls */
   Time interval;

public:
   Timer();

   void set_interval(Time new_interval) { interval = new_interval; }

   /* return the number of ms until next interval; this will be <= 0 if
    * this event has already occured and not received */
   int remaining() const;

   /* return true if the interval has elapsed, false otherwise;
    * reset the interval if it's elapsed */
   bool go();

   /* reset timer */
   void reset();

   /* force timer to go on next call */
   void force();
};

#endif
