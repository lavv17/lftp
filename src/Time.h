#ifndef TIME_CLASSES_H
#define TIME_CLASSES_H

class Time {
	time_t tsec;
	int tms;

public:
	/* set the time to now */
	void set_now();

	/* clear to the beginning of time */
	void clear() { tsec = tms = 0; }

	int sec() const { return tsec; }
	/* int usec() const { return ms; } */
	int ms() const { return (tsec * 1000) + tms; }

	/* all integer operations are in milliseconds */
	/* these are mostly operators which are in use; add others as needed */
	Time operator + (int ms) const;
	Time &operator += (int ms);

	Time operator - (Time rhs) const;
	Time &operator -= (Time rhs);

	bool operator < (Time cmp) const;
	bool operator >= (Time cmp) const { return !(*this < cmp); }

	Time() { clear(); }
};

class Timer {
	Time last;

public:
	Timer();

	/* return 0 if ms have elapsed since last 0 return or instantiation;
	 * otherwise return the number of ms left */
	int go(int ms);

	/* reset timer */
	void reset();

	/* force timer to go on next call */
	void force();
};

#endif
