/*
 *  avgspeed.cpp
 *  Class to compute moving average speed
 *
 *  Created by Arno Bakker
 *  Copyright 2009 Delft University of Technology. All rights reserved.
 *
 */
#include "avgspeed.h"

using namespace swift;

MovingAverageSpeed::MovingAverageSpeed(tint speed_interval, tint fudge)
{
    speed_interval_ = speed_interval;
    fudge_ = fudge;
	t_start_ = usec_time() - fudge_;
	t_end_ = t_start_;
	speed_ = 0.0;
	resetstate_ = false;
}


void MovingAverageSpeed::AddPoint(uint64_t amount)
{
	// Arno, 2012-01-04: Resetting this measurement includes not adding
	// points for a few seconds after the reset, to accomodate the case
	// of going from high speed to low speed and content still coming in.
	//
	if (resetstate_) {
		if ((t_start_ + speed_interval_/2) > usec_time()) {
			return;
		}
		resetstate_ = false;
	}

    tint t = usec_time();
    speed_ = (speed_ * ((double)(t_end_ - t_start_)/((double)TINT_SEC)) + (double)amount) / ((t - t_start_)/((double)TINT_SEC) + 0.0001);
    t_end_ = t;
    if (t_start_ < t - speed_interval_)
        t_start_ = t - speed_interval_;
}


double MovingAverageSpeed::GetSpeed()
{
    AddPoint(0);
    return speed_;
}


double MovingAverageSpeed::GetSpeedNeutral()
{
    return speed_;
}


void MovingAverageSpeed::Reset()
{
	resetstate_ = true;
	t_start_ = usec_time() - fudge_;
	t_end_ = t_start_;
	speed_ = 0.0;
}
