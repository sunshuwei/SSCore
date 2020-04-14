// SSPlanet.cpp
// SSCore
//
// Created by Tim DeBenedictis on 3/15/20.
// Copyright © 2020 Southern Stars. All rights reserved.

#include "SSCoordinates.hpp"
#include "SSPlanet.hpp"
#include "SSJPLDEphemeris.hpp"
#include "SSTLE.hpp"

SSPlanet::SSPlanet ( SSObjectType type ) : SSObject ( type )
{
    _id = SSIdentifier();
    _orbit = SSOrbit();
    _Hmag = _Gmag = _radius = HUGE_VAL;
    _position = _velocity = SSVector ( HUGE_VAL, HUGE_VAL, HUGE_VAL );
}

SSPlanet::SSPlanet ( SSObjectType type, SSPlanetID id ) : SSPlanet ( type )
{
    _id = SSIdentifier ( kCatJPLanet, id );
}

// Computes solar system object's heliocentric position and velocity vectors in AU and AU/day.
// Current time (jed) is Julian Ephemeris Date in dynamic time (TDT), not civil time (UTC).
// Light travel time to object (lt) is in days; may be zero for first approximation.
// Returned position (pos) and velocity (vel) vectors are both in fundamental J2000 equatorial frame.

void SSPlanet::computePositionVelocity ( double jed, double lt, SSVector &pos, SSVector &vel )
{
    if ( _type == kTypePlanet )
        computeMajorPlanetPositionVelocity ( (int) _id.identifier(), jed, lt, pos, vel );
    else if ( _type == kTypeMoon )
        computeMoonPositionVelocity ( jed, lt, pos, vel );
    else if ( _type == kTypeAsteroid || _type == kTypeComet )
        computeMinorPlanetPositionVelocity ( jed, lt, pos, vel );
    else if ( _type == kTypeSatellite )
        computeSatellitePositionVelocity ( jed, lt, pos, vel );
}

// Computes major planet's heliocentric position and velocity vectors in AU and AU/day.
// Current time (jed) is Julian Ephemeris Date in dynamic time (TDT), not civil time (UTC).
// Light travel time to planet (lt) is in days; may be zero for first approximation.
// Returned position (pos) and velocity (vel) vectors are both in fundamental J2000 equatorial frame.

void SSPlanet::computeMajorPlanetPositionVelocity ( int id, double jed, double lt, SSVector &pos, SSVector &vel )
{
    if ( SSJPLDEphemeris::compute ( id, jed - lt, false, pos, vel ) )
        return;
 
    static SSMatrix orbMat = SSCoordinates::getEclipticMatrix ( SSCoordinates::getObliquity ( SSTime::kJ2000 ) );
    SSOrbit orb;
    
    if ( id == kMercury )
        orb = SSOrbit::getMercuryOrbit ( jed - lt );
    else if ( id == kVenus )
        orb = SSOrbit::getVenusOrbit ( jed - lt );
    else if ( id == kEarth )
        orb = SSOrbit::getEarthOrbit ( jed - lt );
    else if ( id == kMars )
        orb = SSOrbit::getMarsOrbit ( jed - lt );
    else if ( id == kJupiter )
        orb = SSOrbit::getJupiterOrbit ( jed - lt );
    else if ( id == kSaturn )
        orb = SSOrbit::getSaturnOrbit ( jed - lt );
    else if ( id == kUranus )
        orb = SSOrbit::getUranusOrbit ( jed - lt );
    else if ( id == kNeptune )
        orb = SSOrbit::getNeptuneOrbit ( jed - lt );
    else if ( id == kPluto )
        orb = SSOrbit::getPlutoOrbit ( jed - lt );

    orb.toPositionVelocity ( jed - lt, pos, vel );
    pos = orbMat.multiply ( pos );
    vel = orbMat.multiply ( vel );
}

// Computes asteroid or comet's heliocentric position and velocity vectors in AU and AU/day.
// Current time (jed) is Julian Ephemeris Date in dynamic time (TDT), not civil time (UTC).
// Light travel time to object (lt) is in days; may be zero for first approximation.
// Returned position (pos) and velocity (vel) vectors are both in fundamental J2000 equatorial frame.

void SSPlanet::computeMinorPlanetPositionVelocity ( double jed, double lt, SSVector &pos, SSVector &vel )
{
    static SSMatrix matrix = SSCoordinates::getEclipticMatrix ( SSCoordinates::getObliquity ( SSTime::kJ2000 ) );
    _orbit.toPositionVelocity ( jed - lt, pos, vel );
    pos = matrix.multiply ( pos );
    vel = matrix.multiply ( vel );
}

// Computes moon's heliocentric position and velocity vectors in AU and AU/day.
// Current time (jed) is Julian Ephemeris Date in dynamic time (TDT), not civil time (UTC).
// Light travel time to moon (lt) is in days; may be zero for first approximation.
// Returned position (pos) and velocity (vel) vectors are both in fundamental J2000 equatorial frame.

void SSPlanet::computeMoonPositionVelocity ( double jed, double lt, SSVector &pos, SSVector &vel )
{
    static SSVector primaryPos[10], primaryVel[10];
    static double primaryJED[10] = { 0.0 };

    // Special case: use JPL ephemeris to compute Earth's Moon's heliocentric position and velocity directly.
    
    if ( _id.identifier() == kLuna )
        if ( SSJPLDEphemeris::compute ( 10, jed - lt, false, pos, vel ) )
            return;
    
    // Compute moon's position and velocity relative to its primary planet.
    
    computeMinorPlanetPositionVelocity ( jed, lt, pos, vel );

    // Get primary planet identifier.
    
    int p = (int) _id.identifier() / 100;
    if ( p < 0 || p > 9 )
        p = 0;
    
    // If JED has changed since last time we computed primary's position and velocity, recompute them.
    
    if ( jed != primaryJED[p] )
    {
        computeMajorPlanetPositionVelocity ( p, jed, 0.0, primaryPos[p], primaryVel[p] );
        primaryJED[p] = jed;
    }
    
    // Add primary's position (antedated for light time) and velocity to moon's position and velocity.

    pos += primaryPos[p] - primaryVel[p] * lt;
    vel += primaryVel[p];
}

// Computes Earth satellite's heliocentric position and velocity vectors in AU and AU/day.
// Current time (jed) is Julian Ephemeris Date in dynamic time (TDT), not civil time (UTC).
// Light travel time to satellite (lt) is in days; may be zero for first approximation.
// Returned position (pos) and velocity (vel) vectors are both in fundamental J2000 equatorial frame.

void SSPlanet::computeSatellitePositionVelocity ( double jed, double lt, SSVector &pos, SSVector &vel )
{
    static SSVector earthPos, earthVel;
    static SSMatrix earthMat;
    static double earthJED = 0.0, deltaT = 0.0;
    
    // compute satellite position & velocity relative to Earth, antedated for light time.
    // Recompute Earth's position and velocity relative to Sun if JED has changed.
    // Asssume Earth's velocity is constant over light time duration.
    
    if ( jed != earthJED )
    {
        computeMajorPlanetPositionVelocity ( kEarth, jed, 0.0, earthPos, earthVel );
        earthJED = jed;
        deltaT = SSTime ( jed ).getDeltaT() / SSTime::kSecondsPerDay;
        earthMat = SSCoordinates::getPrecessionMatrix ( jed ).transpose();
    }

    SSTLE tle;
    
    // Satellite's orbit epoch is Julian Date, not JED, so subtract Delta T.
    // Output position and velocity vectors are in km and km/sec; convert to AU and AU/day;
    // Satellite orbit elements are referred to current equator, not J2000 equator,
    // so transform output position and velocity from current to J2000 equatorial frame.
    
    tle.toPositionVelocity ( jed - deltaT - lt, pos, vel );
    
    pos /= SSCoordinates::kKmPerAU;
    vel /= SSCoordinates::kKmPerAU / SSTime::kSecondsPerDay;
    
    pos = earthMat * pos;
    vel = earthMat * vel;
    
    // Add Earth's position (antedated for light time) and velocity to satellite position and velocity.
    
    pos += earthPos - earthVel * lt;
    vel += earthVel;
}

// Returns solar system object's phase angle in radians.
// Object's heliocentric position vector (position) is in AU, but units don't matter.
// Object's apparent direction seen from observer (direction) must be a unit vector.

double SSPlanet::phaseAngle ( SSVector position, SSVector direction )
{
    return acos ( ( position * direction ) / position.magnitude() );
}

// Returns this solar system object's phase angle in radians.
// Object's heliocentric position and apparent direction vectors must already be calculated!

double SSPlanet::phaseAngle ( void )
{
    return phaseAngle ( _position, _direction );
}

// Returns solar system object's illuminated fraction (from 0.0 to 1.0)
// given its phase angle in radians (phase).

double SSPlanet::illumination ( double phase )
{
    return 1.0 + cos ( phase ) / 2.0;
}

// Returns this solar system object's illuminated fraction (from 0.0 to 1.0)
// Object's heliocentric position and apparent direction vectors must already be calculated!

double SSPlanet::illumination ( void )
{
    return illumination ( phaseAngle() );
}

// Computes solar system object visual magnitude.
// Object's distance from sun (rad) and from observer (dist) are both in AU.
// Object's phase angle (phase) is in radians.
// Object's heliocentric position and apparent direction vectors must already be calculated!
// Formulae for major planets from Jean Meeus, "Astronomical Algorithms", pp. 269-270.

float SSPlanet::computeMagnitude ( double rad, double dist, double phase )
{
    int id = (int) _id.identifier();
    double b = radtodeg ( phase ), b2 = b * b, b3 = b2 * b;
    float mag = HUGE_VAL;
    
    if ( id == kSun )
        mag = -26.72 + 5.0 * log10 ( dist );
    else if ( id == kMercury )
        mag = -0.42 + 5.0 * log10 ( rad * dist ) + 0.0380 * b - 0.000273 * b2 + 0.000002 * b3;
    else if ( id == kVenus )
        mag = -4.40 + 5.0 * log10 ( rad * dist ) + 0.0009 * b + 0.000239 * b2 - 0.00000065 * b3;
    else if ( id == kEarth )
        mag = -3.86 + 5.0 * log10 ( rad * dist );
    else if ( id == kMars )
        mag = -1.52 + 5.0 * log10 ( rad * dist ) + 0.016 * b;
    else if ( id == kJupiter )
        mag = -9.40 + 5.0 * log10 ( rad * dist ) + 0.005 * b;
    else if ( id == kSaturn )
    {
        // Compute Saturn's ring plane inclination in radians from dot product of its apparent direction vector
        // and Saturn's north pole direction vector (both unit vectors in J2000 equatorial frame).
        
        static SSVector pole ( SSSpherical ( degtorad ( 40.589 ), degtorad ( 83.537 ) ) );
        double rinc = M_PI_2 - acos ( _direction * pole );
        mag = -8.88 + 5.0 * log10 ( rad * dist ) + 0.044 * b - 2.60 * fabs ( rinc ) + 1.25 * rinc * rinc;
    }
    else if ( id == kUranus )
        mag = -7.19 + 5.0 * log10 ( rad * dist ) + 0.0028 * b;
    else if ( id == kNeptune )
        mag = -6.87 + 5.0 * log10 ( rad * dist );
    else if ( id == kPluto )
        mag = -1.01 + 5.0 * log10 ( rad * dist ) + 0.041 * b;
    else if ( id == kLuna )
        mag = computeAsteroidMagnitude ( rad, dist, phase, 0.21, 0.25 );
    else if ( _type == kTypeMoon )
        mag = computeAsteroidMagnitude ( rad, dist, phase, _Hmag, isinf ( _Gmag ) ? 0.15 : _Gmag );
    else if ( _type == kTypeAsteroid )
        mag = computeAsteroidMagnitude ( rad, dist, phase, _Hmag, _Gmag );
    else if ( _type == kTypeComet )
        mag = computeCometMagnitude ( rad, dist, _Hmag, _Gmag );
    else if ( _type == kTypeSatellite )
        mag = computeSatelliteMagnitude ( dist * SSCoordinates::kKmPerAU, phase, _Hmag );
    
    return mag;
}

// Computes asteroid visual magnitude.
// Asteroid's distance from sun (rad) and from observer (dist) are both in AU.
// Asteroid's phase angle (phase) is in radians.
// Asteroid's absolute magnitude (h) is visual magnitude at 1 AU from Earth and Sun, 100% illumination.
// Asteroid's magnitude parameter (g) describes how it darkens as illumination decreases.
// Formula from Jean Meeus, "Astronomical Algorithms", p. 217.

float SSPlanet::computeAsteroidMagnitude ( double rad, double dist, double phase, double h, double g )
{
    double phi1 = exp ( -3.33 * pow ( tan ( phase / 2.0 ), 0.63 ) );
    double phi2 = exp ( -1.87 * pow ( tan ( phase / 2.0 ), 1.22 ) );
    float mag = ( 1.0 - g ) * phi1 + g * phi2;
    return mag > 0.0 ? h + 5.0 * log10 ( rad * dist ) - 2.5 * log10 ( mag ) : HUGE_VAL;
}

// Computes comet visual magnitude.
// Comet's distance from sun (rad) and from observer (dist) are both in AU.
// Comet's absolute magnitude (h) is visual magnitude at 1 AU from Earth and Sun.
// Comet's magnitude parameter (k) defines how it darkens as distance from Sun increases.
// Formula from Jean Meeus, "Astronomical Algorithms", p. 216.

float SSPlanet::computeCometMagnitude ( double rad, double dist, double h, double k )
{
    return h + 5.0 * log10 ( dist ) + 2.5 * k * log10 ( rad );
}

// Computes satellite visual magnitude.
// Satellite's distance from observer (dist) is in kilometers.
// Satellite's phase angle (phase) is in radians.
// Standard magnitude is at 1000 km range, and 50% illumination.
// Formula from http://www.prismnet.com/~mmccants/tles/mccdesc.html

float SSPlanet::computeSatelliteMagnitude ( double dist, double phase, double stdmag )
{
    double mag = HUGE_VAL;
    
    if ( phase < M_PI )
        mag = stdmag - 15.75 + 2.5 * log10 ( dist * dist / ( ( 1.0 + cos ( phase ) ) / 2.0 ) );
    
    return mag;
}

// Computes this solar system object's position, direction, distance, and magnitude.
// The current Julian Ephemeris Date and observer position are input in the SSCoordinates object (coords).

void SSPlanet::computeEphemeris ( SSCoordinates &coords )
{
    // Compute planet's heliocentric position and velocity at current JED.
    // Compute distance and light time to planet.
    
    computePositionVelocity ( coords.jed, 0.0, _position, _velocity );
    double lt = ( _position - coords.obsPos ).magnitude() / coords.kLightAUPerDay;

    // Recompute planet's position and velocity antedated for light time.
    // Compute apparent direction vector and distance to planet from observer's position.
    // Apply aberration of light.

    computePositionVelocity ( coords.jed, lt, _position, _velocity );
    _direction = ( _position - coords.obsPos ).normalize ( _distance );
    _direction = coords.addAberration ( _direction );
    
    // Compute planet's phase angle and visual magnitude.
    
    double beta = phaseAngle();
    _magnitude = computeMagnitude ( _position.magnitude(), _distance, beta );
}

// Downcasts generic SSObject pointer to SSPlanet pointer.
// Returns nullptr if input pointer is not an instance of SSPlanet!

SSPlanetPtr SSGetPlanetPtr ( SSObjectPtr ptr )
{
    return dynamic_cast<SSPlanet *> ( ptr.get() );
}

// Returns CSV string from planet data, including identifier and names.

string SSPlanet::toCSV ( void )
{
    string csv = SSObject::typeToCode ( _type ) + ",";
    
    if ( _type == kTypeMoon )
        csv += isinf ( _orbit.q ) ? "," : format ( "%.0f,", _orbit.q * SSCoordinates::kKmPerAU );
    else
        csv += isinf ( _orbit.q ) ? "," : format ( "%.8f,", _orbit.q );

    csv += isinf ( _orbit.e ) ? "," : format ( "%.8f,", _orbit.e );
    csv += isinf ( _orbit.i ) ? "," : format ( "%.8f,", _orbit.i * SSAngle::kDegPerRad );
    csv += isinf ( _orbit.w ) ? "," : format ( "%.8f,", _orbit.w * SSAngle::kDegPerRad );
    csv += isinf ( _orbit.n ) ? "," : format ( "%.8f,", _orbit.n * SSAngle::kDegPerRad );
    csv += isinf ( _orbit.m ) ? "," : format ( "%.8f,", _orbit.m * SSAngle::kDegPerRad );
    csv += isinf ( _orbit.mm ) ? "," : format ( "%.8f,", _orbit.mm * SSAngle::kDegPerRad );
    csv += isinf ( _orbit.t ) ? "," : format ( "%.4f,", _orbit.t );
    
    csv += isinf ( _Hmag ) ? "," : format ( "%+.2f,", _Hmag );
    csv += isinf ( _Gmag ) ? "," : format ( "%+.2f,", _Gmag );
    csv += isinf ( _radius ) ? "," : format ( "%.1f,", _radius );

    csv += _id ? _id.toString() + "," : ",";
        
    for ( int i = 0; i < _names.size(); i++ )
        csv += _names[i] + ",";

    return csv;
}

// Allocates a new SSPlanet and initializes it from a CSV-formatted string.
// Returns nullptr on error (invalid CSV string, heap allocation failure, etc.)

SSObjectPtr SSPlanet::fromCSV ( string csv )
{
    vector<string> fields = split ( csv, "," );
    
    SSObjectType type = SSObject::codeToType ( fields[0] );
    if ( type < kTypePlanet || type > kTypeComet || fields.size() < 14 )
        return nullptr;
    
    SSOrbit orbit;
    
    orbit.q = fields[1].empty() ? HUGE_VAL : strtofloat64 ( fields[1] );
    orbit.e = fields[2].empty() ? HUGE_VAL : strtofloat64 ( fields[2] );
    orbit.i = fields[3].empty() ? HUGE_VAL : strtofloat64 ( fields[3] ) * SSAngle::kRadPerDeg;
    orbit.w = fields[4].empty() ? HUGE_VAL : strtofloat64 ( fields[4] ) * SSAngle::kRadPerDeg;
    orbit.n = fields[5].empty() ? HUGE_VAL : strtofloat64 ( fields[5] ) * SSAngle::kRadPerDeg;
    orbit.m = fields[6].empty() ? HUGE_VAL : strtofloat64 ( fields[6] ) * SSAngle::kRadPerDeg;
    orbit.mm = fields[7].empty() ? HUGE_VAL : strtofloat64 ( fields[7] ) * SSAngle::kRadPerDeg;
    orbit.t = fields[8].empty() ? HUGE_VAL : strtofloat64 ( fields[8] );

    if ( orbit.q > 1000.0 )
        orbit.q /= SSCoordinates::kKmPerAU;
    
    float h = fields[9].empty() ? HUGE_VAL : strtofloat ( fields[9] );
    float g = fields[10].empty() ? HUGE_VAL : strtofloat ( fields[10] );
    float r = fields[11].empty() ? HUGE_VAL : strtofloat ( fields[11] );

    SSIdentifier ident;
    if ( type == kTypePlanet || type == kTypeMoon )
        ident = SSIdentifier ( kCatJPLanet, strtoint ( fields[12] ) );
    else
        ident = SSIdentifier::fromString ( fields[12] );

    vector<string> names;
    for ( int i = 13; i < fields.size(); i++ )
        names.push_back ( trim ( fields[i] ) );
    
	SSObjectPtr pObject = SSNewObject ( type );
    SSPlanetPtr pPlanet = SSGetPlanetPtr ( pObject );
    if ( pPlanet == nullptr )
        return nullptr;
    
    pPlanet->setOrbit ( orbit );
    pPlanet->setHMagnitude ( h );
    pPlanet->setGMagnitude ( g );
    pPlanet->setRadius ( r );
    pPlanet->setIdentifier ( ident );
    pPlanet->setNames ( names );

    return pObject;
}
