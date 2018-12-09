#include "FLIP.h"

FLIP::FLIP(Particle* particles, const unsigned num_particles, Mac3d* MACGrid,
		   const double density, const double gravity, const double alpha) 
	: particles_(particles), num_particles_(num_particles), MACGrid_(MACGrid),
	  fluid_density_(density), gravity_mag_(gravity), alpha_(alpha) {
	
}

/**
 * Advance FLIP simulation by one frame
 */
void FLIP::step_FLIP(const double dt, const unsigned long step) {
	/** One FLIP step:
	 * 1. Compute velocity field (particle-to-grid transfer)
	 *    - Particle-to-grid transfer
	 *    - Classify cells (fluid/air)
	 *    - Extrapolate velocity field into air region
	 * 1a. Copy velocity field to intermediate velocity field u^*
	 * 2. Apply external forces (fwd euler on field)
	 * 3. Enforce boundary conditions for grid & solid boundaries
	 * 4. Compute & apply pressure gradients
	 * 5. Update particle velocities
	 * 6. Update particle positions
	 */
	
	// 1.
	compute_velocity_field();

	// 1a.
	MACGrid_->set_uvw_star();

	// 2.
	apply_forces(dt);

	// 3.
	apply_boundary_conditions();

	// 4.
	do_pressures(dt);

	// 5.
	grid_to_particle();
	
	// 6. subsample time interval to satisfy CFL condition
	double dt_new = compute_timestep(dt);
	double num_substeps = std::ceil(dt/dt_new);
	for( int s = 0; s < num_substeps ; ++s ){
		
		// 7.
		advance_particles(dt_new, step);
	}
}

double FLIP::compute_timestep( const double dt ){
	double dt_new;
	
	Eigen::Vector3d vel;
	double u_max = 0;
	double v_max = 0;
	double w_max = 0;
	for( unsigned int n = 0; n < num_particles_; ++n ){
		vel = (particles_ + n)->get_velocity();
		if ( std::abs(vel(0)) > std::abs(u_max) ){
			u_max = vel(0);
		}
		if ( std::abs(vel(1)) > std::abs(v_max) ){
			v_max = vel(1);
		}
		if ( std::abs(vel(2)) > std::abs(w_max) ){
			w_max = vel(2);
		}
	}
	
	if ( u_max == 0 ){
		dt_new = dt;
	} else {
		dt_new = std::abs(MACGrid_->get_cell_sizex()/u_max);
		if ( dt_new > dt){
			dt_new = dt;
		}
	}
	
	if ( v_max == 0 ){
		dt_new = dt;
	} else {
		double tmp = std::abs(MACGrid_->get_cell_sizey()/v_max);
		if ( tmp < dt_new){
			dt_new = tmp;
		}
		if ( dt_new > dt ){
			dt_new = dt;
		}
	}
	
	if ( w_max == 0 ){
		dt_new = dt;
	} else {
		double tmp = std::abs(MACGrid_->get_cell_sizez()/w_max);
		if ( tmp < dt_new){
			dt_new = tmp;
		}
		if ( dt_new > dt ){
			dt_new = dt;
		}
	}
	
	return dt_new;
}

/*** COMPUTE VELOCITY FIELD ***/
void FLIP::compute_velocity_field() {
	// TODO: 1. Compute the velocity field (velocities on grid)
	//  1a. particle-to-grid transfer
	//  1b. classify nonsolid cells as fluid or air
	//  1c. extrapolate velocity field to air cells
	//    -> see SIGGRAPH §6.3
	
/**********************************
 * Particle-to-Grid Transfer
***********************************/

	// Set all grid velocities to zero
	MACGrid_->set_velocities_to_zero();
	MACGrid_->set_weights_to_zero();
	
	// Positions and velocities of a single particle
	Eigen::Vector3d pos;
	Eigen::Vector3d vel;
	
	// Coordinates of a cell
	Eigen::Vector3d cell_coord;
	
	// Sizes of the edges of a cell (in meters)
	double cell_sizex = MACGrid_->get_cell_sizex();
	double cell_sizey = MACGrid_->get_cell_sizey();
	double cell_sizez = MACGrid_->get_cell_sizez();
	
	// Threshold h and h scaled so that it is equal to the distance expressed in number of cells
	double h = 2*cell_sizex;
	int h_scaledx = std::ceil(h/cell_sizex);
	int h_scaledy = std::ceil(h/cell_sizey);
	int h_scaledz = std::ceil(h/cell_sizez);
	
	// Lists of flags for visited grid-velocities: 1 -> visited
	unsigned N = MACGrid_->get_num_cells_x();
	unsigned M = MACGrid_->get_num_cells_y();
	unsigned L = MACGrid_->get_num_cells_z();
	bool* visited_u = new bool[M*L*(N+1)];
	bool* visited_v = new bool[N*L*(M+1)];
	bool* visited_w = new bool[N*M*(L+1)];
	std::fill(visited_u, visited_u + M*L*(N+1), 0);
	std::fill(visited_v, visited_v + N*L*(M+1), 0);
	std::fill(visited_w, visited_w + N*M*(L+1), 0);
	
	// Reset all fluid flags
	MACGrid_->reset_fluid();
	
	// Iterate over all particles and add weighted particles velocities
	// to grid points within a threshold h (in this case equal to the 
	// length of an edge of a cell)
	for( unsigned int n = 0; n < num_particles_; ++n ){
		pos = (particles_ + n)->get_position();
		vel = (particles_ + n)->get_velocity();

		Eigen::Vector3d tmp = MACGrid_->index_from_coord(pos(0), pos(1), pos(2));
		cell_coord << tmp(0), tmp(1), tmp(2);
		
		// Set the cell of the current particle to a fluid-cell
		if ( !(MACGrid_->is_fluid(cell_coord(0), cell_coord(1), cell_coord(2))) and !(MACGrid_->is_solid(cell_coord(0), cell_coord(1), cell_coord(2))) ){
			MACGrid_->set_fluid(cell_coord(0), cell_coord(1), cell_coord(2));
		}
		
		// Coordinates of the points on the grid edges
		Eigen::Vector3d grid_coord;
		grid_coord << 0, 0, 0;
		int nx = MACGrid_->get_num_cells_x();
		int ny = MACGrid_->get_num_cells_y();
		int nz = MACGrid_->get_num_cells_z();
		for( int k = cell_coord(2) - h_scaledz; k <= cell_coord(2) + h_scaledz + 1; ++k ){	
			for( int j = cell_coord(1) - h_scaledy; j <= cell_coord(1) + h_scaledy + 1; ++j ){
				for( int i = cell_coord(0) - h_scaledx; i <= cell_coord(0) + h_scaledx + 1; ++i ){
					if ( ( i >= 0 and j >= 0 and k >= 0 ) ){
						if ( ( i <= nx and j < ny and k < nz ) ){
						
							// Left edge
							grid_coord(0) = (i - 0.5) * cell_sizex;
							grid_coord(1) = j * cell_sizey;
							grid_coord(2) = k * cell_sizez;
							accumulate_u(pos, vel, grid_coord, h, i, j, k);
						}
						
						if ( ( i < nx and j <= ny and k < nz ) ){
							
							// Lower edge
							grid_coord(0) = i * cell_sizex;
							grid_coord(1) = (j - 0.5) * cell_sizey;
							grid_coord(2) = k * cell_sizez;
							accumulate_v(pos, vel, grid_coord, h, i, j, k);
						}
						
						if ( ( i < nx and j < ny and k <= nz ) ){
							
							// Farthest edge (the closer to the origin)
							grid_coord(0) = i * cell_sizex;
							grid_coord(1) = j * cell_sizey;
							grid_coord(2) = (k - 0.5) * cell_sizez;
							accumulate_w(pos, vel, grid_coord, h, i, j, k);
						}
					}
				}
			}
		}
	}
	
	// Normalize grid-velocities
	normalize_accumulated_u( visited_u );
	normalize_accumulated_v( visited_v );
	normalize_accumulated_w( visited_w );
	
	// Extrapolate velocities
	extrapolate_u( visited_u );
	extrapolate_v( visited_v );
	extrapolate_w( visited_w );
	
	delete[] visited_u;
	delete[] visited_v;
	delete[] visited_w;
}

bool FLIP::check_threshold( const Eigen::Vector3d& particle_coord, 
					  const Eigen::Vector3d& grid_coord, 
					  const double h )
{
	if ( (particle_coord - grid_coord).norm() <= h ) {
		return true;
	}
	
	return false;
}

double FLIP::compute_weight( const Eigen::Vector3d& particle_coord, 
							 const Eigen::Vector3d& grid_coord, 
							 const double h )
{
	double r = (particle_coord - grid_coord).norm();
	double diff = std::pow(h, 2) - std::pow(r, 2);
	return ( (315/(64 * M_PI * std::pow(h, 9))) * std::pow(diff, 3) );
}

// Accumulate velocities and weights for u					
void FLIP::accumulate_u( const Eigen::Vector3d& pos,
						 const Eigen::Vector3d& vel,
						 const Eigen::Vector3d& grid_coord,
						 const double h,
						 const int i,
						 const int j,
						 const int k )
{
	if ( check_threshold(pos, grid_coord, h) ){
		double u_prev = MACGrid_->get_u(i, j, k);
		double W_u = compute_weight(pos, grid_coord, h);
		double u_curr = u_prev + (W_u * vel(0));
		
		// Accumulate velocities
		MACGrid_->set_u(i, j, k, u_curr);
		
		// Accumulate weights
		double W_u_prev = MACGrid_->get_weights_u(i, j, k);
		double W_u_curr = W_u_prev + W_u;
		MACGrid_->set_weights_u(i, j, k, W_u_curr);
	}
}

// Accumulate velocities and weights for v
void FLIP::accumulate_v( const Eigen::Vector3d& pos,
						 const Eigen::Vector3d& vel,
						 const Eigen::Vector3d& grid_coord,
						 const double h,
						 const int i,
						 const int j,
						 const int k )
{
	if ( check_threshold(pos, grid_coord, h) ){
		double v_prev = MACGrid_->get_v(i, j, k);
		double W_v = compute_weight(pos, grid_coord, h);
		double v_curr = v_prev + (W_v * vel(1));
		
		// Accumulate velocities
		MACGrid_->set_v(i, j, k, v_curr);
		
		// Accumulate weights
		double W_v_prev = MACGrid_->get_weights_v(i, j, k);
		double W_v_curr = W_v_prev + W_v;
		MACGrid_->set_weights_v(i, j, k, W_v_curr);
	}
}

// Accumulate velocities and weights for w
void FLIP::accumulate_w( const Eigen::Vector3d& pos,
						 const Eigen::Vector3d& vel,
						 const Eigen::Vector3d& grid_coord,
						 const double h,
						 const int i,
						 const int j,
						 const int k )
{
	if ( check_threshold(pos, grid_coord, h) ){
		double w_prev = MACGrid_->get_w(i, j, k);
		double W_w = compute_weight(pos, grid_coord, h);
		double w_curr = w_prev + (W_w * vel(2));
		
		// Accumulate velocities
		MACGrid_->set_w(i, j, k, w_curr);
		
		// Accumulate weights
		double W_w_prev = MACGrid_->get_weights_w(i, j, k);
		double W_w_curr = W_w_prev + W_w;
		MACGrid_->set_weights_w(i, j, k, W_w_curr);
	}
}

void FLIP::normalize_accumulated_u( bool* const visited_u ){
	unsigned N = MACGrid_->get_num_cells_x();
	unsigned M = MACGrid_->get_num_cells_y();
	unsigned L = MACGrid_->get_num_cells_z();
	for( unsigned k = 0; k < L; ++k ){
		for( unsigned j = 0; j < M; ++j ){
			for( unsigned i = 0; i < N+1; ++i ){
				double W_u = MACGrid_->get_weights_u(i, j, k);
				if ( W_u != 0 ){
					double u_prev = MACGrid_->get_u(i, j, k);
					double u_curr = u_prev/W_u;
					MACGrid_->set_u(i, j, k, u_curr);
					*(visited_u + (N+1)*j + i + (N+1)*M*k) = 1;
				}
			}	
		}
	}	
}

void FLIP::normalize_accumulated_v( bool* const visited_v ){
	unsigned N = MACGrid_->get_num_cells_x();
	unsigned M = MACGrid_->get_num_cells_y();
	unsigned L = MACGrid_->get_num_cells_z();
	for( unsigned k = 0; k < L; ++k ){	
		for( unsigned j = 0; j < M+1; ++j ){
			for( unsigned i = 0; i < N; ++i ){
				double W_v = MACGrid_->get_weights_v(i, j, k);
				if ( W_v != 0 ){
					double v_prev = MACGrid_->get_v(i, j, k);
					double v_curr = v_prev/W_v;
					MACGrid_->set_v(i, j, k, v_curr);
					*(visited_v + N*j + i + N*(M+1)*k) = 1;
				}
			}	
		}	
	}
}

void FLIP::normalize_accumulated_w( bool* const visited_w ){
	unsigned N = MACGrid_->get_num_cells_x();
	unsigned M = MACGrid_->get_num_cells_y();
	unsigned L = MACGrid_->get_num_cells_z();
	for( unsigned k = 0; k < (L+1); ++k ){	
		for( unsigned j = 0; j < M; ++j ){
			for( unsigned i = 0; i < N; ++i ){
				double W_w = MACGrid_->get_weights_w(i, j, k);
				if ( W_w != 0 ){
					double w_prev = MACGrid_->get_w(i, j, k);
					double w_curr = w_prev/W_w;
					MACGrid_->set_w(i, j, k, w_curr);
					*(visited_w + N*j + i + N*M*k) = 1;
				}
			}	
		}	
	}
}

void FLIP::extrapolate_u( const bool* const visited_u ){
	// Do the cases for upper and right bound
	unsigned M = MACGrid_->get_num_cells_y();
	unsigned N = MACGrid_->get_num_cells_x();
	unsigned L = MACGrid_->get_num_cells_z();
	unsigned* counter = new unsigned[M*L*(N+1)];
	std::fill(counter, counter + M*L*(N+1), 0);
	for( unsigned k = 0; k < L; ++k ){	
		for( unsigned j = 0; j < M; ++j ){
			for( unsigned i = 0; i < N+1; ++i ){
				if ( *(visited_u + (N+1)*j + i + (N+1)*M*k) ){
					if ( i != 0 and !(*(visited_u + (N+1)*j + (i-1) + (N+1)*M*k)) ){
						double tmp = MACGrid_->get_u(i-1, j, k) * *(counter + (N+1)*j + (i-1) + (N+1)*M*k);
						*(counter + (N+1)*j + (i-1) + (N+1)*M*k) += 1;
						MACGrid_->set_u(i-1, j, k, (tmp + MACGrid_->get_u(i, j, k))/(*(counter + (N+1)*j + (i-1) + (N+1)*M*k)));
					}
					if ( j != 0 and !(*(visited_u + (N+1)*(j-1) + i + (N+1)*M*k)) ){
						double tmp = MACGrid_->get_u(i, j-1, k) * *(counter + (N+1)*(j-1) + i + (N+1)*M*k);
						*(counter + (N+1)*(j-1) + i + (N+1)*M*k) += 1;
						MACGrid_->set_u(i, j-1, k, (tmp + MACGrid_->get_u(i, j, k))/(*(counter + (N+1)*(j-1) + i + (N+1)*M*k)));
					}
					if ( k != 0 and !(*(visited_u + (N+1)*j + i + (N+1)*M*(k-1))) ){
						double tmp = MACGrid_->get_u(i, j, k-1) * *(counter + (N+1)*j + i + (N+1)*M*(k-1));
						*(counter + (N+1)*j + i + (N+1)*M*(k-1)) += 1;
						MACGrid_->set_u(i, j, k-1, (tmp + MACGrid_->get_u(i, j, k))/(*(counter + (N+1)*j + i + (N+1)*M*(k-1))));
					}
					if ( i != N and !(*(visited_u + (N+1)*j + (i+1) + (N+1)*M*k)) ){
						double tmp = MACGrid_->get_u(i+1, j, k) * *(counter + (N+1)*j + (i+1) + (N+1)*M*k);
						*(counter + (N+1)*j + (i+1) + (N+1)*M*k) += 1;
						MACGrid_->set_u(i+1, j, k, (tmp + MACGrid_->get_u(i, j, k))/(*(counter + (N+1)*j + (i+1) + (N+1)*M*k)));
					}
					if ( j != M-1 and !(*(visited_u + (N+1)*(j+1) + i + (N+1)*M*k)) ){
						double tmp = MACGrid_->get_u(i, j+1, k) * *(counter + (N+1)*(j+1) + i + (N+1)*M*k);
						*(counter + (N+1)*(j+1) + i + (N+1)*M*k) += 1;
						MACGrid_->set_u(i, j+1, k, (tmp + MACGrid_->get_u(i, j, k))/(*(counter + (N+1)*(j+1) + i + (N+1)*M*k)));
					}
					if ( k != L-1 and !(*(visited_u + (N+1)*j + i + (N+1)*M*(k+1))) ){
						double tmp = MACGrid_->get_u(i, j, k+1) * *(counter + (N+1)*j + i + (N+1)*M*(k+1));
						*(counter + (N+1)*j + i + (N+1)*M*(k+1)) += 1;
						MACGrid_->set_u(i, j, k+1, (tmp + MACGrid_->get_u(i, j, k))/(*(counter + (N+1)*j + i + (N+1)*M*(k+1))));
					}
				}
			}
		}
	}
	
	delete[] counter;
}

void FLIP::extrapolate_v( const bool* const visited_v ){
	// Do the cases for upper and right bound
	unsigned M = MACGrid_->get_num_cells_y();
	unsigned N = MACGrid_->get_num_cells_x();
	unsigned L = MACGrid_->get_num_cells_z();
	unsigned* counter = new unsigned[N*L*(M+1)];
	std::fill(counter, counter + N*L*(M+1), 0);
	for( unsigned k = 0; k < L; ++k ){	
		for( unsigned j = 0; j < M+1; ++j ){
			for( unsigned i = 0; i < N; ++i ){
				if ( *(visited_v + N*j + i + N*(M+1)*k) ){
					if ( i != 0 and !(*(visited_v + N*j + (i-1) + N*(M+1)*k)) ){
						double tmp = MACGrid_->get_v(i-1, j, k) * *(counter + N*j + (i-1) + N*(M+1)*k);
						*(counter + N*j + (i-1) + N*(M+1)*k) += 1;
						MACGrid_->set_v(i-1, j, k, (tmp + MACGrid_->get_v(i, j, k))/(*(counter + N*j + (i-1) + N*(M+1)*k)));
					}
					if ( j != 0 and !(*(visited_v + N*(j-1) + i + N*(M+1)*k)) ){
						double tmp = MACGrid_->get_v(i, j-1, k) * *(counter + N*(j-1) + i + N*(M+1)*k);
						*(counter + N*(j-1) + i + N*(M+1)*k) += 1;
						MACGrid_->set_v(i, j-1, k, (tmp + MACGrid_->get_v(i, j, k))/(*(counter + N*(j-1) + i + N*(M+1)*k)));
					}
					if ( k != 0 and !(*(visited_v + N*j + i + N*(M+1)*(k-1))) ){
						double tmp = MACGrid_->get_v(i, j, k-1) * *(counter + N*j + i + N*(M+1)*(k-1));
						*(counter + N*j + i + N*(M+1)*(k-1)) += 1;
						MACGrid_->set_v(i, j, k-1, (tmp + MACGrid_->get_v(i, j, k))/(*(counter + N*j + i + N*(M+1)*(k-1))));
					}
					if ( i != N-1 and !(*(visited_v + N*j + (i+1) + N*(M+1)*k)) ){
						double tmp = MACGrid_->get_v(i+1, j, k) * *(counter + N*j + (i+1) + N*(M+1)*k);
						*(counter + N*j + (i+1) + N*(M+1)*k) += 1;
						MACGrid_->set_v(i+1, j, k, (tmp + MACGrid_->get_v(i, j, k))/(*(counter + N*j + (i+1) + N*(M+1)*k)));
					}
					if ( j != M and !(*(visited_v + N*(j+1) + i + N*(M+1)*k)) ){
						double tmp = MACGrid_->get_v(i, j+1, k) * *(counter + N*(j+1) + i + N*(M+1)*k);
						*(counter + N*(j+1) + i + N*(M+1)*k) += 1;
						MACGrid_->set_v(i, j+1, k, (tmp + MACGrid_->get_v(i, j, k))/(*(counter + N*(j+1) + i + N*(M+1)*k)));
					}
					if ( k != L-1 and !(*(visited_v + N*j + i + N*(M+1)*(k+1))) ){
						double tmp = MACGrid_->get_v(i, j, k+1) * *(counter + N*j + i + N*(M+1)*(k+1));
						*(counter + N*j + i + N*(M+1)*(k+1)) += 1;
						MACGrid_->set_v(i, j, k+1, (tmp + MACGrid_->get_v(i, j, k))/(*(counter + N*j + i + N*(M+1)*(k+1))));
					}
				}
			}
		}
	}
	
	delete[] counter;
}

void FLIP::extrapolate_w( const bool* const visited_w ){
	// Do the cases for upper and right bound
	unsigned M = MACGrid_->get_num_cells_y();
	unsigned N = MACGrid_->get_num_cells_x();
	unsigned L = MACGrid_->get_num_cells_z();
	unsigned* counter = new unsigned[N*M*(L+1)];
	std::fill(counter, counter + N*M*(L+1), 0);
	for( unsigned k = 0; k < L+1; ++k ){	
		for( unsigned j = 0; j < M; ++j ){
			for( unsigned i = 0; i < N; ++i ){
				if ( *(visited_w + N*j + i + N*M*k) ){
					if ( i != 0 and !(*(visited_w + N*j + (i-1) + N*M*k)) ){
						double tmp = MACGrid_->get_w(i-1, j, k) * *(counter + N*j + (i-1) + N*M*k);
						*(counter + N*j + (i-1) + N*M*k) += 1;
						MACGrid_->set_w(i-1, j, k, (tmp + MACGrid_->get_w(i, j, k))/(*(counter + N*j + (i-1) + N*M*k)));
					}
					if ( j != 0 and !(*(visited_w + N*(j-1) + i + N*M*k)) ){
						double tmp = MACGrid_->get_w(i, j-1, k) * *(counter + N*(j-1) + i + N*M*k);
						*(counter + N*(j-1) + i + N*M*k) += 1;
						MACGrid_->set_w(i, j-1, k, (tmp + MACGrid_->get_w(i, j, k))/(*(counter + N*(j-1) + i + N*M*k)));
					}
					if ( k != 0 and !(*(visited_w + N*j + i + N*M*(k-1))) ){
						double tmp = MACGrid_->get_w(i, j, k-1) * *(counter + N*j + i + N*M*(k-1));
						*(counter + N*j + i + N*M*(k-1)) += 1;
						MACGrid_->set_w(i, j, k-1, (tmp + MACGrid_->get_w(i, j, k))/(*(counter + N*j + i + N*M*(k-1))));
					}
					if ( i != N-1 and !(*(visited_w + N*j + (i+1) + N*M*k)) ){
						double tmp = MACGrid_->get_w(i+1, j, k) * *(counter + N*j + (i+1) + N*M*k);
						*(counter + N*j + (i+1) + N*M*k) += 1;
						MACGrid_->set_w(i+1, j, k, (tmp + MACGrid_->get_w(i, j, k))/(*(counter + N*j + (i+1) + N*M*k)));
					}
					if ( j != M-1 and !(*(visited_w + N*(j+1) + i + N*M*k)) ){
						double tmp = MACGrid_->get_w(i, j+1, k) * *(counter + N*(j+1) + i + N*M*k);
						*(counter + N*(j+1) + i + N*M*k) += 1;
						MACGrid_->set_w(i, j+1, k, (tmp + MACGrid_->get_w(i, j, k))/(*(counter + N*(j+1) + i + N*M*k)));
					}
					if ( k != L and !(*(visited_w + N*j + i + N*M*(k+1))) ){
						double tmp = MACGrid_->get_w(i, j, k+1) * *(counter + N*j + i + N*M*(k+1));
						*(counter + N*j + i + N*M*(k+1)) += 1;
						MACGrid_->set_w(i, j, k+1, (tmp + MACGrid_->get_w(i, j, k))/(*(counter + N*j + i + N*M*(k+1))));
					}
				}
			}
		}
	}
	
	delete[] counter;
}

/*** APPLY EXTERNAL FORCES ***/
void FLIP::apply_forces(const double dt) {
	// Compute&apply external forces (gravity, vorticity confinement, ...) 
	// Apply them to the velocity field via forward euler
	// Only worry about gravity for now
	auto& g = MACGrid_;
	const unsigned N = g->get_num_cells_x();
	const unsigned M = g->get_num_cells_y();
	const unsigned L = g->get_num_cells_z();
	
	// Iterate over cells & update: dv = dt*g
	for(unsigned k = 0; k < L; ++k){
		for (unsigned j = 0; j <= M; ++j){
			for (unsigned i = 0; i < N; ++i){
				g->set_v(i, j, k, g->get_v(i,j,k) - dt*gravity_mag_);
			}
		}
	}
}


/*** BOUNDARY CONDITIONS ***/
void FLIP::apply_boundary_conditions() {
	// Enforce boundary conditions for grid & solid boundaries
	unsigned nx = MACGrid_->get_num_cells_x();
	unsigned ny = MACGrid_->get_num_cells_y();
	unsigned nz = MACGrid_->get_num_cells_z();
	// Solid walls
	for(unsigned k = 0; k < nz; ++k){	
		for(unsigned j = 0; j < ny; ++j){
			for(unsigned i = 0; i < nx; ++i){
				bool ijk_solid = MACGrid_->is_solid(i,j,k);
				if (ijk_solid || MACGrid_->is_solid(i+1,j,k))
					MACGrid_->set_u(i+1, j, k, 0);
				if (ijk_solid || MACGrid_->is_solid(i,j+1,k))
					MACGrid_->set_v(i, j+1, k, 0);
				if (ijk_solid || MACGrid_->is_solid(i,j,k+1))
					MACGrid_->set_w(i, j, k+1, 0);
			}
		}
	}

	// Outer (system) boundaries
	for (unsigned k = 0; k < nz; k++) {	
		for (unsigned i = 0; i < nx; i++) {
			//if (MACGrid_->get_v(i, 0, k) < 0)
				MACGrid_->set_v(i, 0, k, 0);
			//if (MACGrid_->get_v(i, ny, k) > 0)
				MACGrid_->set_v(i, ny, k, 0);
		}
	}
	for (unsigned k = 0; k < nz; k++) {	
		for (unsigned j = 0; j < ny; j++) {
			//if (MACGrid_->get_u(0, j, k) < 0)
				MACGrid_->set_u(0, j, k, 0);
			//if (MACGrid_->get_u(nx, j, k) > 0)
				MACGrid_->set_u(nx, j, k, 0);
		}
	}
	for (unsigned j = 0; j < ny; j++) {
		for (unsigned i = 0; i < nx; i++) {
			//if (MACGrid_->get_w(i, j, 0) < 0)
				MACGrid_->set_w(i, j, 0, 0);
			//if (MACGrid_->get_w(i, j, nz) > 0)
				MACGrid_->set_w(i, j, nz, 0);
		}
	}
}


/*** PRESSURE SOLVING ***/
void FLIP::do_pressures(const double dt) {
	// Compute & apply pressure gradients to field
	
	// Compute A matrix
	compute_pressure_matrix();

	// Compute rhs d
	compute_pressure_rhs(dt);

	// Solve for p: Ap = d (MICCG(0))
	using namespace Eigen;
	//using solver_t = ConjugateGradient<SparseMatrix<double>, Lower|Upper>;
	//using solver_t = SimplicialLDLT<SparseMatrix<double>, Lower|Upper>;
	using solver_t = ConjugateGradient<SparseMatrix<double>, Lower|Upper, IncompleteCholesky<double> >;

	//MatrixXd A = A_;
	solver_t solver;
	solver.setMaxIterations(100);
	solver.compute(A_);
	//VectorXd p = A.fullPivLu().solve(d_);
	VectorXd p = solver.solve(d_);

	// Copy pressures to MAC grid
	MACGrid_->set_pressure(p);

	// Apply pressure gradients to velocity field
	//     -> see SIGGRAPH §4
	apply_pressure_gradients(dt);
}

void FLIP::compute_pressure_matrix() {
	// Compute matrix for pressure solve and store in A_
	//  See eq. (4.19) and (4.24) in SIGGRAPH notes
	
	std::vector< Mac3d::Triplet_t > triplets;
	
	unsigned nx = MACGrid_->get_num_cells_x();
	unsigned ny = MACGrid_->get_num_cells_y();
	unsigned nz = MACGrid_->get_num_cells_z();

	unsigned cellidx = 0;
	for (unsigned k = 0; k < nz; ++k) {	
		for (unsigned j = 0; j < ny; ++j) {
			for (unsigned i = 0; i < nx; ++i, ++cellidx) {
				// Copy diagonal entry
				auto& diag_e = MACGrid_->get_a_diag()[i + j*nx + nx*ny*k];
				triplets.push_back(diag_e);

				// Compute off-diagonal entries
				if (MACGrid_->is_fluid(i, j, k)) {
					// x-adjacent cells
					if (i+1 < nx && MACGrid_->is_fluid(i+1, j, k)) {
							triplets.push_back(Mac3d::Triplet_t(cellidx, cellidx+1, -1));
							// Use symmetry to avoid computing (i-1,j,k) separately
							triplets.push_back(Mac3d::Triplet_t(cellidx+1, cellidx, -1));
					}
					// y-adjacent cells
					if (j+1 < ny && MACGrid_->is_fluid(i, j+1, k)) {
							triplets.push_back(Mac3d::Triplet_t(cellidx, cellidx + nx, -1));
							// Use symmetry to avoid computing (i,j-1,k) separately
							triplets.push_back(Mac3d::Triplet_t(cellidx + nx, cellidx, -1));
					}
					// z-adjacent cells
					if (k+1 < nz && MACGrid_->is_fluid(i, j, k+1)) {
							triplets.push_back(Mac3d::Triplet_t(cellidx, cellidx + nx*ny, -1));
							// Use symmetry to avoid computing (i,j-1) separately
							triplets.push_back(Mac3d::Triplet_t(cellidx + nx*ny, cellidx, -1));
					}
				} // if is_fluid(i,j,k)
			}
		} // outer for
	}

	//TODO: only resize A_ and d_ at beginning of sim
	A_.resize(nx*ny*nz, nx*ny*nz);
	A_.setZero();
	A_.setFromTriplets(triplets.begin(), triplets.end());
}

void FLIP::compute_pressure_rhs(const double dt) {
	// Compute right-hand side of the pressure equations and store in d_
	//  See eq. (4.19) and (4.24) in SIGGRAPH notes
	//   Note: u_{solid} = 0
	unsigned nx = MACGrid_->get_num_cells_x();
	unsigned ny = MACGrid_->get_num_cells_y();
	unsigned nz = MACGrid_->get_num_cells_z();

	// Alias for MAC grid
	auto& g = MACGrid_;
	
	//TODO: only resize A_ and d_ at beginning of sim
	d_.resize(nx*ny*nz);
	d_.setZero();
	
	unsigned cellidx = 0;
	for (unsigned k = 0; k < nz; ++k) {
		for (unsigned j = 0; j < ny; ++j) {
			for (unsigned i = 0; i < nx; ++i, ++cellidx) {
				if (g->is_fluid(i,j,k)) {
					// get_u(i,j) = u_{ (i-1/2, j) }
					double d_ij = -(g->get_u(i+1,j,k) - g->get_u(i,j,k));
					d_ij -= g->get_v(i,j+1,k) - g->get_v(i,j,k);
					d_ij -= g->get_w(i,j,k+1) - g->get_w(i,j,k);
					
					// Note: u_{solid} = 0
		
					// Check each adjacent cell. If solid, alter term as in (4.24)
					// Consider cells outside of the boundary as solid
					// (i+1, j, k)
					if ((i < (nx-1) && g->is_solid(i+1,j,k)) || i == nx-1) {
						d_ij += g->get_u(i+1,j,k);
					}
					
					// (i-1, j, k)
					if ((i > 0 && g->is_solid(i-1,j,k)) || i == 0) {
						d_ij += g->get_u(i,j,k);
					}
		
					// (i, j+1, k)
					if ((j < (ny-1) && g->is_solid(i,j+1,k)) || j == ny-1) {
						d_ij += g->get_v(i,j+1,k);
					}
					
					// (i, j-1, k)
					if ((j > 0 && g->is_solid(i,j-1,k)) || j == 0) {
						d_ij += g->get_v(i,j,k);
					}
					
					// (i, j, k+1)
					if ((k < (nz-1) && g->is_solid(i,j,k+1)) || k == nz-1) {
						d_ij += g->get_w(i,j,k+1);
					}
					
					// (i, j, k-1)
					if ((k > 0 && g->is_solid(i,j,k-1)) || k == 0) {
						d_ij += g->get_w(i,j,k);
					}
					// *TODO: cell x and y size should always be the same -> enforce via sim params?
					d_(cellidx) = fluid_density_ * g->get_cell_sizex() * d_ij / dt;
				} else { // if is_fluid(i,j,k)
					d_(cellidx) = 0;
				}
			}
		}
	}
}

void FLIP::apply_pressure_gradients(const double dt) {
	// Apply pressure gradients to velocity field
	unsigned nx = MACGrid_->get_num_cells_x();
	unsigned ny = MACGrid_->get_num_cells_y();
	unsigned nz = MACGrid_->get_num_cells_z();
	// MACGrid alias
	auto& g = MACGrid_;
	// *TODO: correct cell dimensions
	double dx = g->get_cell_sizex();
	for (unsigned k = 0; k < nz; ++k) {	
		for (unsigned j = 0; j < ny; ++j) {
			for (unsigned i = 0; i < nx; ++i) {
				if (i != 0) {
					// get_u(i,j,k) = u_{ (i-1/2, j, k) }
					// See SIGGRAPH eq. (4.4)
					double du = (g->get_pressure(i,j,k) - g->get_pressure(i-1,j,k));
					du *= (dt/(dx*fluid_density_));
					g->set_u(i,j,k, g->get_u(i,j,k) - du);
				}
				if (j != 0) {
					// get_v(i,j,k) = v_{ (i, j-1/2, k) }
					// See SIGGRAPH eq. (4.5)
					double dv = (g->get_pressure(i,j,k) - g->get_pressure(i,j-1,k));
					dv *= (dt/(dx*fluid_density_));
					g->set_v(i,j,k, g->get_v(i,j,k) - dv);
				}
				if (k != 0) {
					// get_w(i,j,k) = w_{ (i, j, k-1/2) }
					// See SIGGRAPH eq. (4.5)
					double dw = (g->get_pressure(i,j,k) - g->get_pressure(i,j,k-1));
					dw *= (dt/(dx*fluid_density_));
					g->set_w(i,j,k, g->get_w(i,j,k) - dw);
				}
			}
		}
	}
}



/*** UPDATE PARTICLE VELOCITIES & MOVE PARTICLES ***/
void FLIP::grid_to_particle(){
	// FLIP grid to particle transfer
	//  -> See slides Fluids II, FLIP_explained.pdf
	// FLIP: alpha = 0.
	// PIC: alpha = 1.
	double alpha = alpha_;
	int nx = MACGrid_->get_num_cells_x();
	int ny = MACGrid_->get_num_cells_y();
	int nz = MACGrid_->get_num_cells_z();
	
	for(unsigned i = 0; i < num_particles_; ++i){
		//Store the initial positions and velocities of the particles
		Eigen::Vector3d initial_position = (particles_+i)->get_position();
		Eigen::Vector3d initial_velocity = (particles_+i)->get_velocity();
		auto initial_idx = MACGrid_->index_from_coord(initial_position(0), 
													  initial_position(1),
													  initial_position(2));
		
		//Initialization of the variables
		Eigen::Vector3d interp_u_star;
		interp_u_star.setZero();
		Eigen::Vector3d interp_u_n1;
		interp_u_n1.setZero();
		Eigen::Vector3d u_update;
		u_update.setZero();

		double x = initial_position[0];
		double y = initial_position[1];
		double z = initial_position[2];
		
		//With u*, v* and w* we can make the interpolation interp(u*, x_p),
		//with the new u, v and w we can make the interpolation interp(u_n1, x_p)
		
		//Update the u-velocity (trilinear interpolation)
		interp_u_star[0] = MACGrid_->get_interp_u(x,y,z,true);
		interp_u_n1[0] = MACGrid_->get_interp_u(x,y,z);
		
		//Update the v-velocity (trilinear interpolation)
		interp_u_star[1] = MACGrid_->get_interp_v(x,y,z,true);
		interp_u_n1[1] = MACGrid_->get_interp_v(x,y,z);
		
		//Update the w-velocity (trilinear interpolation)
		interp_u_star[2] = MACGrid_->get_interp_w(x,y,z,true);
		interp_u_n1[2] = MACGrid_->get_interp_w(x,y,z);

		//Update the final velocity of the particles
		
		// Use PIC on boundary, blend PIC+FLIP elsewhere
		if (initial_idx(0) == 0 or initial_idx(0) == nx-1
		 or initial_idx(1) == 0 or initial_idx(1) == ny-1
		 or initial_idx(2) == 0 or initial_idx(2) == nz-1){
			u_update = initial_velocity*(1 - std::min(1., 2*alpha)) + interp_u_n1 - interp_u_star*(1 - std::min(1., 2*alpha));
		} else {
			u_update = initial_velocity*(1 - alpha) + interp_u_n1 - interp_u_star*(1 - alpha);
		}
		
		(particles_ + i)->set_velocity(u_update);
	}
}


void FLIP::advance_particles(const double dt, const unsigned step) {
	//Se una particles esce dal sistema o entra in un solido, rispingerla dentro.
	// TODO: update particle positions 
	//  - Use RK2 interpolator	
	for(unsigned n = 0; n < num_particles_; ++n){
		Eigen::Vector3d pos_curr = (particles_ + n)->get_position();
		Eigen::Vector3d vel = (particles_ + n)->get_velocity();
		
		Eigen::Vector3d pos_next;
		
		// Euler estimate
		Eigen::Vector3d pos_half = pos_curr + 0.5*dt*vel;
		
		double size_x = MACGrid_->get_grid_size()(0);
		double size_y = MACGrid_->get_grid_size()(1);
		double size_z = MACGrid_->get_grid_size()(2);
		double cell_sizex = MACGrid_->get_cell_sizex();
		double cell_sizey = MACGrid_->get_cell_sizey();
		double cell_sizez = MACGrid_->get_cell_sizez();
		double x_half = pos_half(0);
		double y_half = pos_half(1);
		double z_half = pos_half(2);
		
		// Check if pos_half is out of the grid
		if ((x_half <= -0.5*cell_sizex) or (x_half >= size_x - 0.5*cell_sizex)
		 or (y_half <= -0.5*cell_sizey) or (y_half >= size_y - 0.5*cell_sizey)
		 or (z_half <= -0.5*cell_sizez) or (z_half >= size_z - 0.5*cell_sizez)){
			continue;
		}
		
		// RK2
		pos_next(0) = pos_curr(0) + dt*MACGrid_->get_interp_u(x_half, y_half, z_half);
		pos_next(1) = pos_curr(1) + dt*MACGrid_->get_interp_v(x_half, y_half, z_half);
		pos_next(2) = pos_curr(2) + dt*MACGrid_->get_interp_w(x_half, y_half, z_half);
		
		double x = pos_next(0);
		double y = pos_next(1);
		double z = pos_next(2);
		
		// Check if the particle exits the grid
		if (x <= -0.5*cell_sizex) {
			pos_next(0) = 0.;
		}
		if (x >= size_x - 0.5*cell_sizex) {
			pos_next(0) = size_x - cell_sizex;
		}
		if (y <= -0.5*cell_sizey) {
			pos_next(1) = 0.;
		}
		if (y >= size_y - 0.5*cell_sizey) {
			pos_next(1) = size_y - cell_sizey;
		}
		if (z <= -0.5*cell_sizez) {
			pos_next(2) = 0.;
		}
		if (z >= size_z - 0.5*cell_sizez) {
			pos_next(2) = size_z - cell_sizez;
		}

		// Check if the particle enters in a solid
		auto prev_indices = MACGrid_->index_from_coord(pos_curr(0), pos_curr(1), pos_curr(2));
		auto new_indices = MACGrid_->index_from_coord(pos_next(0), pos_next(1), pos_curr(2));
		double sx = MACGrid_->get_cell_sizex();
		double sy = MACGrid_->get_cell_sizey();
		double sz = MACGrid_->get_cell_sizez();
		//TODO: correctly shift particles & velocities
		if (MACGrid_->is_solid(new_indices(0), new_indices(1), new_indices(2))) {
			if (prev_indices(0)  > new_indices(0))
				pos_next(0) = (prev_indices(0) - 0.25) * sx;
			else if (prev_indices(0) < new_indices(0))
				pos_next(0) = (prev_indices(0) + 0.25) * sx;

			if (prev_indices(1) > new_indices(1))
				pos_next(1) = (prev_indices(1) - 0.25) * sy;
			else if (prev_indices(1) < new_indices(1))
				pos_next(1) = (prev_indices(1) + 0.25) * sy;
				
			if (prev_indices(2) > new_indices(2))
				pos_next(2) = (prev_indices(2) - 0.25) * sz;
			else if (prev_indices(2) < new_indices(2))
				pos_next(2) = (prev_indices(2) + 0.25) * sz;
		}

		(particles_ + n)->set_position(pos_next);
	}
}
