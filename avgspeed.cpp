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
    t_start_ = usec_time() - fudge;
    t_end_ = t_start_;
    speed_ = 0.0;
}


void MovingAverageSpeed::AddPoint(uint64_t amount)
{
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
