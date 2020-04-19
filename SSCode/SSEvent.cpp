// SSEvent.cpp
// SSTest
//
// Created by Tim DeBenedictis on 4/18/20.
// Copyright © 2020 Southern Stars. All rights reserved.

#include "SSEvent.hpp"

// Computes the hour angle when an object with declination (dec)
// as seen from latitude (lat) reaches an altitude (alt) above
// or below th horison.  All angles are in radians.
// If the object's altitude is always greater than (alt), returns kPi.
// If the altitude is always less than (alt), returns zero.

SSAngle SSEvent::semiDiurnalArc ( SSAngle lat, SSAngle dec, SSAngle alt )
{
    double cosha = ( sin ( alt ) - sin ( dec ) * sin ( lat ) ) / ( cos ( dec ) * cos ( lat ) );
    
    if ( cosha >= 1.0 )
        return 0.0;
    else if ( cosha <= -1.0 )
        return SSAngle::kPi;
    else
        return acos ( cosha );
}

// Given an object's equatorial coordinates (ra,dec) at a particular instant (time),
// returns the object's time of rising, transit, or setting above a horizon altitude (alt),
// as seen from a particular geographic longitude (lon) and latitude (lat).
// The event to compute (sign) is -1 = rising, 0 = transit, +1 = setting.
// All angles are in radians; north and east are positive.
// The returned time will always be within 0.5 days of the input time, except:
// if the object does set below the horizon altitude, return +INFINITY;
// if the object does rise above the horizon altitude, returns -INFINITY.
// The object's equatorial coordinates, (ra,dec), should be given for the same
// precessional epoch as the input time!
// This function ignores the object's motion during day, OK for stars;
// but for moving solar system objects, use riseTransitSetSearch().
// For point objects, use horizon altitude -0.5 degrees to account for refraction.
// For the Sun and Moon, use horizon altitude -50 arcminutes.
// For civil, nautical, and astronomical twilight, use a horizon
// altitude of -6, -12, and -18 degrees, respectively.

SSTime SSEvent::riseTransitSet ( SSTime time, SSAngle ra, SSAngle dec, int sign, SSAngle lon, SSAngle lat, SSAngle alt )
{
    // Compute the object's hour angle when it reaches the rise/set altitude.

    SSAngle ha = semiDiurnalArc ( lat, dec, alt );

    // If the object never sets, return infinity;
    // if it never rises, return negative infinity.
         
    if ( ha == SSAngle::kPi && sign != 0 )
        return ( INFINITY );
    
    if ( ha == 0.0 )
        return ( -INFINITY );
    
    // Compute the local sidereal time
    
    SSAngle lst = time.getSiderealTime ( lon );
    
    // Now compute the angular distance that the earth needs to turn
    // through to make the object reach the horizon altitude.
    // Reduce it to the range -kPi to +kPi
    
    SSAngle theta = ( ra - lst + sign * ha ).modPi();
    
    // Obtain the time of rising or setting by adding the amount of time
    // the earth takes to rotate through the angle calculated above to the
    // current time.
        
    return time + ( theta / SSAngle::kTwoPi / SSTime::kSiderealPerSolarDays );
}

// Wrapper for the above which takes input location in the SSCoordinates object (obj)
// and gets equatorial RA and Dec from an object (obj); other params (sign,alt) are as above.

SSTime SSEvent::riseTransitSet ( SSTime time, SSCoordinates &coords, SSObjectPtr pObj, int sign, SSAngle alt )
{
    SSSpherical loc = coords.getLocation();
    SSSpherical equ ( coords.transform ( kFundamental, kEquatorial, pObj->getDirection() ) );
    return riseTransitSet ( time, equ.lon, equ.lat, sign, loc.lon, loc.lat, alt );
}

// Computes the time of an object's rise, transit, or set that is closest to an initial starting time (time).
// All other parameters (coords, pObj, sign, alt) are the same as for riseTransitSet().
// If the object does not rise, returns -INFINITY; if it does not set, returns +INFINITY.
// After return, both coords and pObj will be modified (recomputed for the time of the event).
// Will not work for objects which rise and set multiple times per day, e.g. artifical satellites.

SSTime SSEvent::riseTransitSetSearch ( SSTime time, SSCoordinates &coords, SSObjectPtr pObj, int sign, SSAngle alt )
{
    SSTime lasttime = time;
    int i = 0, imax = 10;
    double precision = 1.0 / SSTime::kSecondsPerDay;
             
    // Iteratively compute the object's position and predict increasingly accurate
    // estimates of the object's rise/transit/set time, until the estimate of the time
    // converges to the specified precision, or we perform the maximum number of iterations.

    do
    {
        lasttime = time;
        coords.setTime ( time );
        pObj->computeEphemeris ( coords );
        time = riseTransitSet ( time, coords, pObj, sign, alt );
        i++;
    }
    while ( fabs ( time - lasttime ) > precision && ! isinf ( time ) && i < imax );
            
    return ( time );
}

// Computes the time an object's rises, transits, or sets on a particular local day (today).
// All other parameters (coords, pObj, sign, alt) are the same as for riseTransitSet().
// If the object does not rise or set on the specified local day, returns -INFINITY or +INFINITY.
// After return, both coords and pObj will be modified (recomputed for the time of the event).
// Will not work for objects which rise and set multiple times per day, e.g. artifical satellites.

SSTime SSEvent::riseTransitSetSearchDay ( SSTime today, SSCoordinates &coords, SSObjectPtr pObj, int sign, SSAngle alt )
{
    // Find the julian dates that correspond to the start and end of the local day.
         
    SSTime start = today.getLocalMidnight();
    SSTime end = start + 1.0;

    // Search for the object's exact rise/set time, starting from the middle of the local day.

    SSTime time = riseTransitSetSearch ( start + 0.5, coords, pObj, sign, alt );
    
    // If the resulting rise/set time is after the end of the day,
    // start searching again from the middle of the previous day;
    // similarly, if the resulting rise/set time is before the start
    // of the current day, start searching again from the middle
    // of the following day.

    if ( time > end )
        time = riseTransitSetSearch ( start - 0.5, coords, pObj, sign, alt );
    else if ( time < start )
        time = riseTransitSetSearch ( end + 0.5, coords, pObj, sign, alt );

    // If the resulting rise/set time is still before the beginning or
    // after the end of the local day, the object does not rise or set
    // on that day; return positive/negative infinity to indicate this.
    
    if ( time > end || time < start )
    {
        if ( sign == -1 )
            time = -INFINITY;
        else
            time = INFINITY;
    }
                
    return ( time );
}

// Returns circumstances of an object's overhead pass on a given local day (today) as seen from a
// particular location (coords), with the altitude for rising and setting (alt) in radians.
// Transit time and elevation are stored in the returned pass's transit struct.
// If the object does not rise or set on the specified local day, returns -INFINITY or +INFINITY.
// After return, both coords and pObj will be restored to their original states.
// Will not work for objects which rise and set multiple times per day, e.g. artifical satellites.

SSPass SSEvent::riseTransitSet ( SSTime today, SSCoordinates &coords, SSObjectPtr pObj, SSAngle alt )
{
    SSTime savetime = coords.getTime();
    SSPass pass = { 0.0 };
    SSSpherical hor = { INFINITY, INFINITY, INFINITY };
    
    pass.rising.time = riseTransitSetSearchDay ( today, coords, pObj, kRise, alt );
    if ( ! isinf ( pass.rising.time ) )
    {
        hor = coords.transform ( kFundamental, kHorizon, pObj->getDirection() );
        pass.rising.azm = hor.lon;
        pass.rising.alt = hor.lat;
    }
    
    pass.transit.time = riseTransitSetSearchDay ( today, coords, pObj, kTransit, 0.0 );
    if ( ! isinf ( pass.transit.time ) )
    {
        hor = coords.transform ( kFundamental, kHorizon, pObj->getDirection() );
        pass.transit.azm = hor.lon;
        pass.transit.alt = hor.lat;
    }

    pass.setting.time = riseTransitSetSearchDay ( today, coords, pObj, kSet, alt );
    if ( ! isinf ( pass.setting.time ) )
    {
        hor = coords.transform ( kFundamental, kHorizon, pObj->getDirection() );
        pass.setting.azm = hor.lon;
        pass.setting.alt = hor.lat;
    }

    // Reset original time and restore object's original ephemeris
    
    coords.setTime ( savetime );
    pObj->computeEphemeris ( coords );
    
    return pass;
}

// Searches for satellite passes seen from a location (coords) between two Julian dates (start to stop).
// Passes start when satellite's apparent altitude rises above a minimum threshold (minAlt) in radians;
// passes end when satellite's elevation falls below that threshold.  Peak elevation and time thereof are
// also recorded in each pass's transit struct. The method returns the total number of passes found, and
// returns all pass circumstances in the vector of SSPass structs.
// After return, both coords and pObj will be restored to their original states.

int SSEvent::findSatellitePasses ( SSCoordinates &coords, SSObjectPtr pSat, SSTime start, SSTime stop, double minAlt, vector<SSPass> &passes )
{
    SSTime  savetime = coords.getTime();
    SSTime  time = 0, step = 0;
    SSPass  pass = { 0 };
    SSAngle azm = 0, alt = 0, maxAlt = 0, oldAlt = 0;
    SSSpherical hor = { INFINITY, INFINITY, INFINITY };
    
    for ( time = start; time <= stop; time += step )
    {
        // Advance time and recompute satellite's position;
        // obtain its current elevation and azimuth.
        
        coords.setTime ( time );
        pSat->computeEphemeris ( coords );
        hor = coords.transform ( kFundamental, kHorizon, pSat->getDirection() );
        azm = hor.lon;
        alt = hor.lat;
        
        // When the satellite reaches 1 degree below the horizon,
        // change time step to 1 second for more precision.
        // Otherwise use a coarse 1-minute time step for speed.
        
        if ( hor.lat > -1.0 * SSAngle::kDegPerRad )
            step = 1.0 / SSTime::kSecondsPerDay;
        else
            step = 1.0 / SSTime::kMinutesPerDay;
        
        if ( time > start )
        {
            // If satellite is above elevation threshold now,
            // but below it on previous step, pass starts now.
            
            if ( alt > minAlt && oldAlt < minAlt )
            {
                pass.rising.time = time;
                pass.rising.azm = azm;
                pass.rising.alt = alt;
            }
            
            // Search for peak elevation.

            if ( alt > maxAlt )
            {
                pass.transit.time = time;
                pass.transit.azm = azm;
                pass.transit.alt = alt;
                maxAlt = alt;
            }
            
            // If satellite is below elevation threshold now,
            // but above it on previous step, pass starts now.

            if ( oldAlt > minAlt && alt < minAlt )
            {
                pass.setting.time = time;
                pass.setting.azm = azm;
                pass.setting.alt = alt;
                passes.push_back ( pass );
                maxAlt = 0.0;
            }
        }
        
        oldAlt = alt;
    }
    
    // Reset original time and restore satellite's original ephemeris
    
    coords.setTime ( savetime );
    pSat->computeEphemeris ( coords );

    return (int) passes.size();
}
