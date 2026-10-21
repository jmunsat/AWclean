/*
 * AWclean - plain-English overlay on top of Airwindows Consolidated
 *
 * This source released under the MIT License, found in ~/LICENSE.md.
 */

#ifndef AWCLEAN_OVERLAYTYPES_H
#define AWCLEAN_OVERLAYTYPES_H

namespace awclean
{
struct Param
{
    const char *label;
    const char *formula; // of x (0..1) and sr; nullptr means use the Airwindows readout
    const char *unit;
    int decimals; // -1 means pick automatically
    const char *lo, *hi;
    const char *steps; // '|' separated names for stepped controls
    const char *note;
};

struct Entry
{
    const char *name;
    const char *title;
    const char *category;
    const char *note;
    int nParams;
    const Param *params;
};
} // namespace awclean
#endif
