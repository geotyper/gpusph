/*
 * BuyancyTest.cc
 *
 *  Created on: 20 juin 2014
 *      Author: alexisherault
 */

#include "BuoyancyTest.h"
#include <cmath>
#include <iostream>

#include "GlobalData.h"
#include "Cube.h"
#include "Sphere.h"
#include "Point.h"
#include "Vector.h"


BuoyancyTest::BuoyancyTest(const GlobalData *_gdata) : Problem(_gdata)
{
	// Size and origin of the simulation domain
	lx = 1.0;
	ly = 1.0;
	lz = 1.0;
	H = 0.7;

	m_size = make_double3(lx, ly, lz);
	m_origin = make_double3(0.0, 0.0, 0.0);

	SETUP_FRAMEWORK(
		kernel<WENDLAND>,
		viscosity<ARTVISC>,
		//viscosity<SPSVISC>,
		boundary<DYN_BOUNDARY>
	);

	// SPH parameters
	set_deltap(0.02); //0.008
	m_simparams.slength = 1.3*m_deltap;
	m_simparams.kernelradius = 2.0;
	m_simparams.dt = 0.0003f;
	m_simparams.dtadaptfactor = 0.3;
	m_simparams.buildneibsfreq = 10;
	m_simparams.ferrari = 0;
	m_simparams.tend = 20.0f; //0.00036f

	// Free surface detection
	m_simparams.surfaceparticle = false;
	m_simparams.savenormals = false;

	// Vorticity
	m_simparams.vorticity = false;

	// We have no moving boundary
	m_simparams.mbcallback = false;

	// Physical parameters
	H = 0.6f;
	m_physparams.gravity = make_float3(0.0, 0.0, -9.81f);
	double g = length(m_physparams.gravity);
	m_physparams.set_density(0, 1000.0, 7.0f, 20.f);

    //set p1coeff,p2coeff, epsxsph here if different from 12.,6., 0.5
	m_physparams.dcoeff = 5.0f*g*H;
	m_physparams.r0 = m_deltap;

	m_physparams.kinematicvisc = 1.0e-6f;
	m_physparams.artvisccoeff = 0.3f;
	m_physparams.epsartvisc = 0.01*m_simparams.slength*m_simparams.slength;

	// Allocate data for floating bodies
	allocate_ODE_bodies(1);
	dInitODE();				// Initialize ODE
	m_ODEWorld = dWorldCreate();	// Create a dynamic world
	m_ODESpace = dHashSpaceCreate(0);
	m_ODEJointGroup = dJointGroupCreate(0);
	dWorldSetGravity(m_ODEWorld, m_physparams.gravity.x, m_physparams.gravity.y, m_physparams.gravity.z);	// Set gravity(x, y, z)

	add_writer(VTKWRITER, 0.1);

	// Name of problem used for directory creation
	m_name = "BuoyancyTest";
}


BuoyancyTest::~BuoyancyTest(void)
{
	release_memory();
}


void BuoyancyTest::release_memory(void)
{
	parts.clear();
	boundary_parts.clear();
}


int BuoyancyTest::fill_parts()
{
	double r0 = m_physparams.r0;
	const double dp = m_deltap;
	const int layers = 4;

	Cube experiment_box = Cube(Point(0, 0, 0), lx, ly, lz);

	Cube fluid = Cube(Point(dp*layers, dp*layers, dp*layers),
		lx - 2.0*dp*layers, ly - 2.0*dp*layers, H);
	planes[0] = dCreatePlane(m_ODESpace, 0.0, 0.0, 1.0, 0.0);
	planes[1] = dCreatePlane(m_ODESpace, 1.0, 0.0, 0.0, 0.0);
	planes[2] = dCreatePlane(m_ODESpace, -1.0, 0.0, 0.0, -lx);
	planes[3] = dCreatePlane(m_ODESpace, 0.0, 1.0, 0.0, 0.0);
	planes[4] = dCreatePlane(m_ODESpace, 0.0, -1.0, 0.0, -ly);

	boundary_parts.reserve(2000);
	parts.reserve(14000);

	experiment_box.SetPartMass(m_deltap, m_physparams.rho0[0]);
	experiment_box.FillIn(boundary_parts, m_deltap, layers, false);
	fluid.SetPartMass(m_deltap, m_physparams.rho0[0]);
	fluid.Fill(parts, m_deltap, true);

	const int object_type = 2;
	Object *floating;
	switch (object_type) {
		case 0: {
			double olx = 10.0*m_deltap;
			double oly = 10.0*m_deltap;
			double olz = 10.0*m_deltap;
			cube  = Cube(Point(lx/2.0 - olx/2.0, ly/2.0 - oly/2.0, H/2.0 - olz/2.0), olx, oly, olz);
			floating = &cube;
			}
			break;

		case 1: {
			double r = 6.0*m_deltap;
			sphere = Sphere(Point(lx/2.0, ly/2.0, H/2.0 - r/4.0), r);
			floating = &sphere;
			}
			break;

		case 2: {
			double R = lx*0.2;
			double r = 4.0*m_deltap;
			torus = Torus(Point(lx/2.0, ly/2.0, H/2.0), Vector(0, 0, 1), R, r);
			floating = &torus;
			}
			break;
	}

	floating->SetMass(m_deltap, m_physparams.rho0[0]*0.5);
	floating->SetPartMass(m_deltap, m_physparams.rho0[0]);
	floating->FillIn(floating->GetParts(), m_deltap, layers);
	floating->Unfill(parts, m_deltap*0.85);

	floating->ODEBodyCreate(m_ODEWorld, m_deltap);
	if (object_type != 2)
		floating->ODEGeomCreate(m_ODESpace, m_deltap);
	add_ODE_body(floating);
	dBodySetLinearVel(floating->ODEGetBody(), 0.0, 0.0, 0.0);
	dBodySetAngularVel(floating->ODEGetBody(), 0.0, 0.0, 0.0);
	PointVect & rbparts = get_ODE_body(0)->GetParts();
	std::cout << "Rigid body " << 1 << ": " << rbparts.size() << " particles \n";
	std::cout << "totl rb parts:" << get_ODE_bodies_numparts() << "\n";
	return parts.size() + boundary_parts.size() + get_ODE_bodies_numparts();
}


void BuoyancyTest::ODE_near_callback(void *data, dGeomID o1, dGeomID o2)
{
	const int N = 10;
	dContact contact[N];

	int n = dCollide(o1, o2, N, &contact[0].geom, sizeof(dContact));
	for (int i = 0; i < n; i++) {
		contact[i].surface.mode = dContactBounce;
		contact[i].surface.mu   = dInfinity;
		contact[i].surface.bounce     = 0.0; // (0.0~1.0) restitution parameter
		contact[i].surface.bounce_vel = 0.0; // minimum incoming velocity for bounce
		dJointID c = dJointCreateContact(m_ODEWorld, m_ODEJointGroup, &contact[i]);
		dJointAttach (c, dGeomGetBody(contact[i].geom.g1), dGeomGetBody(contact[i].geom.g2));
	}
}


void
BuoyancyTest::copy_to_array(BufferList &buffers)
{
	float4 *pos = buffers.getData<BUFFER_POS>();
	hashKey *hash = buffers.getData<BUFFER_HASH>();
	float4 *vel = buffers.getData<BUFFER_VEL>();
	particleinfo *info = buffers.getData<BUFFER_INFO>();

	std::cout << "Boundary parts: " << boundary_parts.size() << std::endl;
	for (uint i = 0; i < boundary_parts.size(); ++i) {
		float ht = H - boundary_parts[i](2);
		if (ht < 0)
			ht = 0.0;
		float rho = density(ht, 0);
		vel[i] = make_float4(0, 0, 0, rho);
		info[i] = make_particleinfo(BOUNDPART, 0, i);
		calc_localpos_and_hash(boundary_parts[i], info[i], pos[i], hash[i]);
	}
	uint j = boundary_parts.size();
	std::cout << "Boundary part mass: " << pos[j-1].w << std::endl;

	for (uint k = 0; k < m_simparams.numODEbodies; k++) {
		PointVect & rbparts = get_ODE_body(k)->GetParts();
		std::cout << "Rigid body " << k << ": " << rbparts.size() << " particles ";
		for (uint i = 0; i < rbparts.size(); i++) {
			uint ij = i + j;
			float ht = H - rbparts[i](2);
			if (ht < 0)
				ht = 0.0;
			float rho = density(ht, 0);
			rho = m_physparams.rho0[0];
			vel[ij] = make_float4(0, 0, 0, rho);
			info[ij] = make_particleinfo(OBJECTPART, k, i );
			calc_localpos_and_hash(rbparts[i], info[ij], pos[ij], hash[ij]);
		}
		j += rbparts.size();
		std::cout << ", part mass: " << pos[j-1].w << "\n";
	}

	std::cout << "Fluid parts: " << parts.size() << std::endl;
	for (uint i = 0; i < parts.size(); ++i) {
		uint ij = i+j;
		float ht = H - parts[i](2);
		if (ht < 0)
			ht = 0.0;
		float rho = density(ht, 0);
		vel[ij] = make_float4(0, 0, 0, rho);
		info[ij] = make_particleinfo(FLUIDPART, 0, ij);
		calc_localpos_and_hash(parts[i], info[ij], pos[ij], hash[ij]);
	}
	j += parts.size();

	std::cout << "Fluid part mass: " << pos[j-1].w << std::endl;

	std::flush(std::cout);
}