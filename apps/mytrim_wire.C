/***************************************************************************
 *   Copyright (C) 2008 by Daniel Schwen   *
 *   daniel@schwen.de   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <queue>

/*#include <mytrim/simconf.h>
#include <mytrim/element.h>
#include <mytrim/material.h>
#include <mytrim/sample_wire.h>
#include <mytrim/ion.h>
#include <mytrim/trim.h>
#include <mytrim/invert.h>*/
#include "simconf.h"
#include "element.h"
#include "material.h"
#include "sample_wire.h"
#include "ion.h"
#include "trim.h"
#include "invert.h"

#include "functions.h"

using namespace MyTRIM_NS;

int main(int argc, char *argv[])
{
  char fname[200];
  if (argc != 8)
  {
    fprintf(stderr, "syntax:\n%s basename Eion[eV] angle[deg] numpka zpka mpka r[nm]\n", argv[0]);
    return 1;
  }

  Real epka  = atof(argv[2]);
  Real theta = atof(argv[3]) * M_PI/180.0; // 0 = perpendicular to wire
  int numpka  = atoi(argv[4]);
  int   zpka  = atoi(argv[5]);
  Real mpka  = atof(argv[6]);
  Real dwire  = atof(argv[7])*20.0;

  // seed randomnumber generator from system entropy pool
  FILE *urand = fopen("/dev/random", "r");
  int seed;
  if (fread(&seed, sizeof(int), 1, urand) != 1) return 1;
  fclose(urand);
  r250_init(seed<0 ? -seed : seed); // random generator goes haywire with neg. seed

  // initialize global parameter structure and read data tables from file
  SimconfType * simconf = new SimconfType;

  // initialize sample structure
  SampleWire *sample = new SampleWire(dwire, dwire, 100.0);

  // initialize trim engine for the sample
  const int z1 = 29; //Cu
  const int z2 = 22; //Ti
  const int z3 = 47; //Ag
  TrimVacMap *trim = new TrimVacMap(simconf, sample, z1, z2, z3); // GaCW

  MaterialBase *material;
  ElementBase *element;

  material = new MaterialBase(simconf, (56.0*8.920 + 38.0*4.507 + 8.0*10.490)/(56.0+38.0+8.0)); // rho
  element = new ElementBase;
  element->_Z = z1; // Cu
  element->_m = 63.546;
  element->_t = 56.0;
  material->_element.push_back(element);
  element = new ElementBase;
  element->_Z = z2; // Ti
  element->_m = 47.867;
  element->_t = 38.0;
  material->_element.push_back(element);
  element = new ElementBase;
  element->_Z = z3; // Ag
  element->_m = 107.87;
  element->_t = 8.0;
  material->_element.push_back(element);
  material->prepare(); // all materials added
  sample->material.push_back(material); // add material to sample

  // create a FIFO for recoils
  std::queue<IonBase*> recoils;

  IonBase *pka;

  const int mx = 20, my = 20;
  int imap[mx][my][3];
  for (int e = 0; e < 3; e++)
    for (int x = 0; x < mx; x++)
      for (int y = 0; y < my; y++)
        imap[x][y][e] = 0;

  // 10000 ions
  for (int n = 0; n < numpka; n++)
  {
    if (n % 1000 == 0) fprintf(stderr, "pka #%d\n", n+1);

    pka = new IonBase;
    pka->gen = 0; // generation (0 = PKA)
    pka->tag = -1;
    pka->_Z = zpka; // S
    pka->_m = mpka;
    pka->_E  = epka;

    pka->_dir(0) = 0.0;
    pka->_dir(1) = -cos(theta);
    pka->_dir(2) = sin(theta);

    v_norm(pka->_dir);

    pka->_pos(0) = dr250() * sample->w[0];
    pka->_pos(2) = dr250() * sample->w[2];

    // wire surface
    pka->_pos(1) = sample->w[1] / 2.0 * (1.0 + std::sqrt(1.0 - sqr((pka->_pos(0) / sample->w[0]) * 2.0 - 1.0))) - 0.5;

    pka->setEf();
    recoils.push(pka);

    while (!recoils.empty())
    {
      pka = recoils.front();
      recoils.pop();
      sample->averages(pka);

      // do ion analysis/processing BEFORE the cascade here

      if (pka->_Z == zpka )
      {
        //printf( "p1 %f\t%f\t%f\n", pka->_pos(0), pka->_pos(1), pka->_pos(2));
      }

      // follow this ion's trajectory and store recoils
      // printf("%f\t%d\n", pka->_E, pka->_Z);
      trim->trim(pka, recoils);

      // do ion analysis/processing AFTER the cascade here

      // ion is still in sample
      if ( sample->lookupMaterial(pka->_pos) != 0)
      {
        int x, y;
        x = ((pka->_pos(0) * mx) / sample->w[0]);
        y = ((pka->_pos(1) * my) / sample->w[1]);
        x -= int(x/mx) * mx;
        y -= int(y/my) * my;

        // keep track of interstitials for the two constituents
        if (pka->_Z == z1) imap[x][y][0]++;
        else if (pka->_Z == z2) imap[x][y][1]++;
        else if (pka->_Z == z3) imap[x][y][2]++;
      }

      // done with this recoil
      delete pka;
    }
  }

  const char *elnam[3] = { "Cu", "Ti", "Ag" };

  FILE *intf, *vacf, *netf;
  // e<numberofelementsinwire
  for (int e = 0; e < 3; e++)
  {
    snprintf(fname, 199, "%s.%s.int", argv[1], elnam[e]);
    intf = fopen(fname, "wt");
    snprintf(fname, 199, "%s.%s.vac", argv[1], elnam[e]);
    vacf = fopen(fname, "wt");
    snprintf(fname, 199, "%s.%s.net", argv[1], elnam[e]);
    netf = fopen(fname, "wt");

    for (int y = 0; y <= my; y++)
    {
      for (int x = 0; x <= mx; x++)
      {
        Real x1 = Real(x)/Real(mx)*sample->w[0];
        Real y1 = Real(y)/Real(my)*sample->w[1];
        fprintf(intf, "%f %f %d\n", x1, y1, (x<mx && y<my) ? imap[x][y][e] : 0);
        fprintf(vacf, "%f %f %d\n", x1, y1, (x<mx && y<my) ? trim->vmap[x][y][e] : 0);
        fprintf(netf, "%f %f %d\n", x1, y1, (x<mx && y<my) ? (imap[x][y][e] - trim->vmap[x][y][e]) : 0);
      }
      fprintf(intf, "\n");
      fprintf(vacf, "\n");
      fprintf(netf, "\n");
    }

    fclose(intf);
    fclose(vacf);
    fclose(netf);
  }

  return EXIT_SUCCESS;
}
