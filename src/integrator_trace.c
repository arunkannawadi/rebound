/**
 * @file    integrator_trace.c
 * @brief   trace, a modified version of John Chambers' MERCURY algorithm
 *          using the IAS15 integrator and WHFast. It works with planet-planry
 *          collisions, test particles, and additional forces.
 * @author  Hanno Rein, Dan Tamayo
 *
 * @section LICENSE
 * Copyright (c) 2019 Hanno Rein, Dan Tamayo
 *
 * This file is part of rebound.
 *
 * rebound is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * rebound is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rebound.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "rebound.h"
#include "integrator.h"
#include "gravity.h"
#include "integrator_trace.h"
#include "integrator_whfast.h"
#include "integrator_mercurius.h"
#include "integrator_bs.h"
#include "collision.h"
#define MIN(a, b) ((a) > (b) ? (b) : (a))    ///< Returns the minimum of a and b
#define MAX(a, b) ((a) > (b) ? (a) : (b))    ///< Returns the maximum of a and b

double reb_integrator_trace_switch_default(struct reb_simulation* const r, int i, int j){
  struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
  double dcriti = 0.0;
  double dcritj = 0.0;

  // For debugging
  double ai = 0.0;
  double aj = 0.0;

  const double m0 = r->particles[0].m;
  if (r->particles[i].m != 0){
    /*
    const double dxi  = r->particles[i].x;  // in dh
    const double dyi  = r->particles[i].y;
    const double dzi  = r->particles[i].z;
    const double dvxi = r->particles[i].vx - r->particles[0].vx;
    const double dvyi = r->particles[i].vy - r->particles[0].vy;
    const double dvzi = r->particles[i].vz - r->particles[0].vz;
    const double _ri = sqrt(dxi*dxi + dyi*dyi + dzi*dzi);
    const double v2i = dvxi*dvxi + dvyi*dvyi + dvzi*dvzi;

    const double GM = r->G*(m0+r->particles[i].m);
    const double ai = GM*_ri / (2.*GM - _ri*v2i);
    */
    struct reb_orbit o1 = reb_tools_particle_to_orbit(r->G, r->particles[i], r->particles[0]);
    ai = o1.a;
    dcriti = ri_tr->hillfac*ai*cbrt(r->particles[i].m/(3.*m0));
  }

  if (r->particles[j].m != 0){
    /*
    const double dxj  = r->particles[j].x;  // in dh
    const double dyj  = r->particles[j].y;
    const double dzj  = r->particles[j].z;
    const double dvxj = r->particles[j].vx - r->particles[0].vx;
    const double dvyj = r->particles[j].vy - r->particles[0].vy;
    const double dvzj = r->particles[j].vz - r->particles[0].vz;
    const double _rj = sqrt(dxj*dxj + dyj*dyj + dzj*dzj);
    const double v2j = dvxj*dvxj + dvyj*dvyj + dvzj*dvzj;

    const double GM = r->G*(m0+r->particles[j].m);
    const double aj = GM*_rj / (2.*GM - _rj*v2j);
    */
    struct reb_orbit o2 = reb_tools_particle_to_orbit(r->G, r->particles[j], r->particles[0]);
    aj = o2.a;
    dcritj = ri_tr->hillfac*aj*cbrt(r->particles[j].m/(3.*m0));
  }

  const double dx = r->particles[i].x - r->particles[j].x;
  const double dy = r->particles[i].y - r->particles[j].y;
  const double dz = r->particles[i].z - r->particles[j].z;
  const double d = sqrt(dx*dx + dy*dy + dz*dz);

  // Use traditional switching function
  double dcritmax = MAX(dcriti,dcritj);
  dcritmax *= 1.21;

  double fcond = d - dcritmax;
  return fcond;
}

double reb_integrator_trace_switch_velocity(const struct reb_simulation* const r, int i, int j){
  struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
  const double vfac = ri_tr->vfac;

  const double dx = r->particles[i].x - r->particles[j].x;
  const double dy = r->particles[i].y - r->particles[j].y;
  const double dz = r->particles[i].z - r->particles[j].z;
  const double d = sqrt(dx*dx + dy*dy + dz*dz);

  const double vx = r->particles[i].vx - r->particles[j].vx;
  const double vy = r->particles[i].vy - r->particles[j].vy;
  const double vz = r->particles[i].vz - r->particles[j].vz;
  const double vsquared = vx*vx + vy*vy + vz*vz;

  const double vel_term = sqrt(vsquared + r->G * (r->particles[i].m + r->particles[j].m) / d);
  const double inv_vel = 1. / vel_term;
  const double F_vel = d * inv_vel;
  const double fcond = vfac * r->dt - F_vel;

  return fcond;
}

double reb_integrator_trace_peri_switch_default(const struct reb_simulation* const r, int j){
  struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
  const double peri = ri_tr->peri;

  const double dx = r->particles[0].x - r->particles[j].x;
  const double dy = r->particles[0].y - r->particles[j].y;
  const double dz = r->particles[0].z - r->particles[j].z;
  const double d = sqrt(dx*dx + dy*dy + dz*dz);

  double fcond_peri = d - peri;
  return fcond_peri;
}

double reb_integrator_trace_switch_fdot_peri(const struct reb_simulation* const r, int j){
  struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
  const double vfacp = ri_tr->vfac_p;

  /*
    const double mu = r->G*(r->particles[i].m+r->particles[j].m);
    const double vcircsquared = mu/d;
    const double a = -mu/(vsquared - 2.*vcircsquared);

    const double vdiffsquared = vsquared - vcircsquared;
    const double vr = (dx*vx + dy*vy + dz*vz)/d;
    const double rvr = d*vr;
    const double muinv = 1./mu;
    const double ex = muinv*(vdiffsquared*dx - rvr*vx );
    const double ey = muinv*(vdiffsquared*dy - rvr*vy );
    const double ez = muinv*(vdiffsquared*dz - rvr*vz );
    const double e = sqrt( ex*ex + ey*ey + ez*ez );
    const double ome = 1. - e;
    const double F_vel_peri = 2 * M_PI * sqrt(((ome*ome*ome)/(1+e))*(a*a*a/mu));
  */

  const double dx = r->particles[j].x;
  const double dy = r->particles[j].y;
  const double dz = r->particles[j].z;
  const double d2 = dx*dx + dy*dy + dz*dz;

  struct reb_orbit o = reb_tools_particle_to_orbit(r->G, r->particles[j], r->particles[0]);
  const double fdot = o.h / (d2);
  const double F_vel_peri = (2 * M_PI / fdot) / r->dt; // effective period
  double fcond_peri = F_vel_peri - vfacp;

  return fcond_peri;
}

double reb_integrator_trace_switch_vdiff_peri(const struct reb_simulation* const r, int j){
  struct reb_orbit o = reb_tools_particle_to_orbit(r->G, r->particles[j], r->particles[0]);
  if (o.a < 0.0){
    return 1.; //unbound orbits have no pericenter approach
  }
  struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
  const double vfacp = ri_tr->vfac_p;

  const double dvx = r->particles[j].vx;
  const double dvy = r->particles[j].vy;
  const double dvz = r->particles[j].vz;
  const double dv = sqrt(dvx*dvx + dvy*dvy + dvz*dvz);

  const double vcirc = sqrt(r->G * (r->particles[0].m + r->particles[j].m) / o.a);
  const double vdiff = dv / vcirc;
  double fcond_peri = vfacp - vdiff;

  return fcond_peri;
}

void reb_integrator_trace_inertial_to_dh(struct reb_simulation* r){
    struct reb_particle* restrict const particles = r->particles;
    struct reb_vec3d com_pos = {0};
    struct reb_vec3d com_vel = {0};
    double mtot = 0.;
    const int N_active = (r->N_active==-1 || r->testparticle_type==1)?r->N:r->N_active;
    const int N = r->N;
    for (int i=0;i<N_active;i++){
        double m = particles[i].m;
        com_pos.x += m * particles[i].x;
        com_pos.y += m * particles[i].y;
        com_pos.z += m * particles[i].z;
        com_vel.x += m * particles[i].vx;
        com_vel.y += m * particles[i].vy;
        com_vel.z += m * particles[i].vz;
        mtot += m;
    }
    com_pos.x /= mtot; com_pos.y /= mtot; com_pos.z /= mtot;
    com_vel.x /= mtot; com_vel.y /= mtot; com_vel.z /= mtot;
    // Particle 0 is also changed to allow for easy collision detection
    for (int i=N-1;i>=0;i--){
        particles[i].x -= particles[0].x;
        particles[i].y -= particles[0].y;
        particles[i].z -= particles[0].z;
        particles[i].vx -= com_vel.x;
        particles[i].vy -= com_vel.y;
        particles[i].vz -= com_vel.z;
    }
    r->ri_tr.com_pos = com_pos;
    r->ri_tr.com_vel = com_vel;
}

void reb_integrator_trace_dh_to_inertial(struct reb_simulation* r){
    struct reb_particle* restrict const particles = r->particles;
    struct reb_particle temp = {0};
    const int N = r->N;
    const int N_active = (r->N_active==-1 || r->testparticle_type==1)?r->N:r->N_active;
    for (int i=1;i<N_active;i++){
        double m = particles[i].m;
        temp.x += m * particles[i].x;
        temp.y += m * particles[i].y;
        temp.z += m * particles[i].z;
        temp.vx += m * particles[i].vx;
        temp.vy += m * particles[i].vy;
        temp.vz += m * particles[i].vz;
        temp.m += m;
    }
    temp.m += r->particles[0].m;
    temp.x /= temp.m;
    temp.y /= temp.m;
    temp.z /= temp.m;
    temp.vx /= particles[0].m;
    temp.vy /= particles[0].m;
    temp.vz /= particles[0].m;
    // Use com to calculate central object's position.
    // This ignores previous values stored in particles[0].
    // Should not matter unless collisions occured.
    particles[0].x = r->ri_tr.com_pos.x - temp.x;
    particles[0].y = r->ri_tr.com_pos.y - temp.y;
    particles[0].z = r->ri_tr.com_pos.z - temp.z;

    for (int i=1;i<N;i++){
        particles[i].x += particles[0].x;
        particles[i].y += particles[0].y;
        particles[i].z += particles[0].z;
        particles[i].vx += r->ri_tr.com_vel.x;
        particles[i].vy += r->ri_tr.com_vel.y;
        particles[i].vz += r->ri_tr.com_vel.z;
    }
    particles[0].vx = r->ri_tr.com_vel.x - temp.vx;
    particles[0].vy = r->ri_tr.com_vel.y - temp.vy;
    particles[0].vz = r->ri_tr.com_vel.z - temp.vz;
}

void reb_integrator_trace_interaction_step(struct reb_simulation* const r, double dt){
    struct reb_particle* restrict const particles = r->particles;
    const int N = r->N;
    struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
    reb_update_acceleration(r);
    for (int i=1;i<N;i++){
        particles[i].vx += dt*particles[i].ax;
        particles[i].vy += dt*particles[i].ay;
        particles[i].vz += dt*particles[i].az;
        //if (i == 2){
        //printf("%e %e %e %e\n", r->t, particles[i].ax, particles[i].ay, particles[i].az);
      //}
    }
}

void reb_integrator_trace_jump_step(struct reb_simulation* const r, double dt){
    struct reb_particle* restrict const particles = r->particles;

    struct reb_simulation_integrator_trace* ri_tr = &(r->ri_tr);
    const int current_L = ri_tr->current_L;

    const int N_active = r->N_active==-1?r->N:r->N_active;
    const int N = r->testparticle_type==0 ? N_active: r->N;
    double px=0., py=0., pz=0.;
    for (int i=1;i<N;i++){
        px += r->particles[i].vx*r->particles[i].m; // in dh
        py += r->particles[i].vy*r->particles[i].m;
        pz += r->particles[i].vz*r->particles[i].m;
    }
    px /= r->particles[0].m;
    py /= r->particles[0].m;
    pz /= r->particles[0].m;
    const int N_all = r->N;
    for (int i=1;i<N_all;i++){
        particles[i].x += dt*px*(1-current_L);
        particles[i].y += dt*py*(1-current_L);
        particles[i].z += dt*pz*(1-current_L);
    }
}

void reb_integrator_trace_com_step(struct reb_simulation* const r, double dt){
    r->ri_tr.com_pos.x += dt*r->ri_tr.com_vel.x;
    r->ri_tr.com_pos.y += dt*r->ri_tr.com_vel.y;
    r->ri_tr.com_pos.z += dt*r->ri_tr.com_vel.z;
}

// Old Kepler
void reb_integrator_trace_whfast_step(struct reb_simulation* const r, double dt){
    //struct reb_particle* restrict const particles = r->particles;
    struct reb_simulation_integrator_trace* ri_tr = &(r->ri_tr);
    const int N = r->N;
    for (int i=1;i<N;i++){
        //if (ri_tr->encounter_map[i] != 0){
          reb_whfast_kepler_solver(r,r->particles,r->G*r->particles[0].m,i,dt); // in dh
        //}
    }
}

void reb_integrator_trace_bs_step(struct reb_simulation* const r, const double _dt){
  struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);

  if (ri_tr->encounterN < 2){
    // No close encounters, skip
    return;
  }

  int i_enc = 0;
  ri_tr->encounterNactive = 0;
  for (unsigned int i=0; i<r->N; i++){
      if(ri_tr->encounter_map_internal[i]){
          struct reb_particle tmp = r->particles[i];      // Copy for potential use for tponly_encounter
          r->particles[i] = ri_tr->particles_backup_try[i]; // Coordinates before WHFast step, overwrite particles with close encounters
          ri_tr->encounter_map[i_enc] = i;
          i_enc++;
          if (r->N_active==-1 || i<r->N_active){
              ri_tr->encounterNactive++;
          }
      }
  }
  //exit(1);

  ri_tr->mode = 1;
  // run
  const double old_dt = r->dt;
  const double old_t = r->t;
  double t_needed = r->t + _dt;
  //reb_integrator_bs_reset(r);

  r->dt = _dt; // start with a small timestep.
  //printf("BEGIN!!! %f %f ", r->t, r->dt);
  while(r->t < t_needed && fabs(r->dt/old_dt)>1e-14 ){
    // In case of overshoot
    if (r->t+r->dt >  t_needed){
      r->dt = t_needed-r->t;
    }

    struct reb_particle star = r->particles[0]; // backup velocity
    r->particles[0].vx = 0; // star does not move in dh
    r->particles[0].vy = 0;
    r->particles[0].vz = 0;

    reb_integrator_bs_part2(r);

    // Now, r->dt is the proposed next step

    r->particles[0].vx = star.vx; // restore every timestep for collisions
    r->particles[0].vy = star.vy;
    r->particles[0].vz = star.vz;
  }

  r->t = old_t;
  r->dt = old_dt;
  ri_tr->mode = 0;
  // return reject;
}

void reb_integrator_trace_kepler_step(struct reb_simulation* const r, const double _dt){
  int rej = 0;
  struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
  memcpy(ri_tr->particles_backup_try,r->particles,r->N*sizeof(struct reb_particle));
  reb_integrator_trace_whfast_step(r, _dt);
  reb_integrator_trace_bs_step(r, _dt);
  // return rej;
}


void reb_integrator_trace_part1(struct reb_simulation* r){
    if (r->var_config_N){
        reb_warning(r,"TRACE does not work with variational equations.");
    }

    struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
    const int N = r->N;

    if (ri_tr->allocatedN<N){
        // These arrays are only used within one timestep.
        // Can be recreated without loosing bit-wise reproducibility.
        ri_tr->particles_backup       = realloc(ri_tr->particles_backup,sizeof(struct reb_particle)*N);

        ri_tr->current_Ks = realloc(ri_tr->current_Ks, sizeof(int)*((N-1)*(N-2))/2);
        ri_tr->encounter_map          = realloc(ri_tr->encounter_map,sizeof(int)*N);
        ri_tr->encounter_map_internal = realloc(ri_tr->encounter_map_internal,sizeof(int)*N);

        // Only need this stuff for Listing 3
        ri_tr->particles_backup_try   = realloc(ri_tr->particles_backup_try,sizeof(struct reb_particle)*N);
        ri_tr->recalculate_coordinates_this_timestep = 1;
        ri_tr->allocatedN = N;
    }
    if (ri_tr->safe_mode || ri_tr->recalculate_coordinates_this_timestep){
        if (ri_tr->is_synchronized==0){
            reb_integrator_trace_synchronize(r);
            reb_warning(r,"TRACE: Recalculating heliocentric coordinates but coordinates were not synchronized before.");
        }
        reb_integrator_trace_inertial_to_dh(r);
        ri_tr->recalculate_coordinates_this_timestep = 0;
    }

    // Calculate collisions only with DIRECT method
    if (r->collision != REB_COLLISION_NONE && r->collision != REB_COLLISION_DIRECT){
        reb_warning(r,"TRACE only works with a direct collision search.");
    }

    // Calculate gravity with special function
    if (r->gravity != REB_GRAVITY_BASIC && r->gravity != REB_GRAVITY_TRACE){
        reb_warning(r,"TRACE has it's own gravity routine. Gravity routine set by the user will be ignored.");
    }

    // Switching functions
    if (ri_tr->S == NULL){
      ri_tr->S = reb_integrator_trace_switch_default;
    }

    if (ri_tr->S_peri == NULL){
      ri_tr->S_peri = reb_integrator_trace_peri_switch_default;
    }

    r->gravity = REB_GRAVITY_TRACE;
    ri_tr->mode = 0;

    // Clear encounter maps
    for (unsigned int i=0; i<r->N; i++){
      ri_tr->encounter_map[i] = 0;
      ri_tr->encounter_map_internal[i] = 0;
    }
    ri_tr->encounter_map_internal[0] = 1;
}

// Particle-particle collision tracking. Explanation is in my notes.
int reb_integrator_trace_pindex(unsigned int i, unsigned int j, int N){
  return (i-1)*N-((i-1)*(2+i)/2)+j-i-1;
}

int reb_integrator_trace_Fcond(struct reb_simulation* const r){
  struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
  const int N = r->N;
  const int Nactive = r->N_active==-1?r->N:r->N_active;

  // Switching functions
  double (*_switch) (const struct reb_simulation* const r, unsigned int i, unsigned int j) = r->ri_tr.S;
  double (*_switch_peri) (const struct reb_simulation* const r, unsigned int j) = r->ri_tr.S_peri;

  int new_c = 0; // New CEs

  // Check for pericenter CE
  // test particles cannot have pericenter CEs
  for (int j = 1; j < Nactive; j++){
    double fcond_peri = _switch_peri(r, j);
    if (fcond_peri < 0.0 && ri_tr->current_L == 0){
      ri_tr->current_L = 1;
      new_c = 1;
      //printf("Flagged %d peri approach at %f %f\n", j, r->t, fcond_peri);
    }
  }

  // Body-body
  // there cannot be TP-TP CEs
  for (int i = 1; i < Nactive; i++){
    for (int j = i + 1; j < N; j++){

      double fcond = _switch(r, i, j);

      if (fcond < 0.0){
        if (ri_tr->encounter_map_internal[i] == 0){
            ri_tr->encounter_map_internal[i] = i;
            ri_tr->encounterN++;
        }
        if (ri_tr->encounter_map_internal[j] == 0){
            ri_tr->encounter_map_internal[j] = j;
            ri_tr->encounterN++;
        }
        // printf("%f %d %d Body-Body CE\n", r->t, i, j);
        // Checks for switching Kij 0->1. Initialized as all 0 the first time of asking.
        if (ri_tr->current_Ks[reb_integrator_trace_pindex(i,j,N)] == 0){
          ri_tr->current_Ks[reb_integrator_trace_pindex(i,j,N)] = 1;
          new_c = 1;
        }
      }
    }
  }

  return new_c;
}

// This is Listing 2
void reb_integrator_trace_part2(struct reb_simulation* const r){
    struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
    const int N = r->N;
    // Make copy of particles
    memcpy(ri_tr->particles_backup,r->particles,N*sizeof(struct reb_particle));
    ri_tr->encounterN = 1;
    ri_tr->current_L = 0;

    for (int i = 0; i < (N-1)*(N-2)/2; i++){
      ri_tr->current_Ks[i] = 0;
    }

    int new_ce = reb_integrator_trace_Fcond(r); // output means nothing at this step for now

    if (ri_tr->current_L){ //more efficient way to check if we need to redo this...
      // Pericenter close encounter detected. We integrate the entire simulation with BS
      ri_tr->encounter_map_internal[0] = 1;
      ri_tr->encounterN = N;
      for (int i = 1; i < N; i++){
        ri_tr->encounter_map_internal[i] = i; // Identity map
      }
      ri_tr->encounterNactive = ((r->N_active==-1)?r->N:r->N_active);
    }

    reb_integrator_trace_interaction_step(r, r->dt/2.);
    reb_integrator_trace_jump_step(r, r->dt/2.);
    reb_integrator_trace_kepler_step(r, r->dt); // always accept this
    reb_integrator_trace_com_step(r,r->dt);
    reb_integrator_trace_jump_step(r, r->dt/2.);
    reb_integrator_trace_interaction_step(r, r->dt/2.);

    // Check for new close_encounters
    if (reb_integrator_trace_Fcond(r)){
      // REJECT STEP
      // reset simulation and try again with new timestep
      for (int i=0; i<N; i++){
          // Reject & reset
          r->particles[i] = ri_tr->particles_backup[i];
      }

      if (ri_tr->current_L){ //more efficient way to check if we need to redo this...
        // Pericenter close encounter detected. We integrate the entire simulation with BS
        ri_tr->encounter_map_internal[0] = 1;
        ri_tr->encounterN = N;
        for (int i = 1; i < N; i++){
          ri_tr->encounter_map_internal[i] = i; // Identity map
        }
        ri_tr->encounterNactive = ((r->N_active==-1)?r->N:r->N_active);
      }

      reb_integrator_trace_interaction_step(r, r->dt/2.);
      reb_integrator_trace_jump_step(r, r->dt/2.);
      reb_integrator_trace_kepler_step(r, r->dt); // always accept this
      reb_integrator_trace_com_step(r,r->dt);
      reb_integrator_trace_jump_step(r, r->dt/2.);
      reb_integrator_trace_interaction_step(r, r->dt/2.);
    }
    ri_tr->is_synchronized = 0;
    if (ri_tr->safe_mode){
        reb_integrator_trace_synchronize(r);
    }

    r->t+=r->dt;
    r->dt_last_done = r->dt;
}

void reb_integrator_trace_synchronize(struct reb_simulation* r){
    struct reb_simulation_integrator_trace* const ri_tr = &(r->ri_tr);
    if (ri_tr->is_synchronized == 0){
        //printf("synchronizing\n");
        r->gravity = REB_GRAVITY_TRACE; // needed here again for SimulationArchive

        ri_tr->mode=0;
        //reb_integrator_trace_jump_step(r,r->dt/2.);
        reb_integrator_trace_dh_to_inertial(r);

        ri_tr->recalculate_coordinates_this_timestep = 1;
        ri_tr->is_synchronized = 1;
    }
}

void reb_integrator_trace_reset(struct reb_simulation* r){
    r->ri_tr.mode = 0;
    r->ri_tr.encounterN = 0;
    r->ri_tr.encounterNactive = 0;
    r->ri_tr.hillfac = 4; // TLu changed to Hernandez (2023)
    r->ri_tr.vfac = 3.;
    r->ri_tr.vfac_p = 3.;

    //r->ri_tr.peri = 0.; // TLu changed to Hernandez (2023)
    r->ri_tr.recalculate_coordinates_this_timestep = 0;
    // Internal arrays (only used within one timestep)
    free(r->ri_tr.particles_backup);
    r->ri_tr.particles_backup = NULL;

    free(r->ri_tr.encounter_map);
    r->ri_tr.encounter_map = NULL;
    r->ri_tr.allocatedN = 0;
    r->ri_tr.allocatedN_additionalforces = 0;

    free(r->ri_tr.particles_backup_try);
    r->ri_tr.particles_backup_try = NULL;

    free(r->ri_tr.current_Ks);
    r->ri_tr.current_Ks = NULL;

    r->ri_tr.current_L = 0;

    free(r->ri_tr.encounter_map_internal);
    r->ri_tr.encounter_map_internal = NULL;

    r->ri_tr.S = NULL;
    r->ri_tr.S_peri = NULL;

}