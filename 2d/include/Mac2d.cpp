#include "Mac2d.h"

double Mac2d::get_u(const int i, const int j){
	assert(i < (N_+1) && j < (M_) && "Index out of bounds!");
	return *(pu_ + (N_+1)*j + i);
}

double Mac2d::get_v(const int i, const int j){
	assert(i < (N_) && j < (M_+1) && "Index out of bounds!");
	return *(pv_ + N_*j + i);
}

double Mac2d::get_pressure(const int i, const int j){
	assert(i < N_ && j < M_ && "Index out of bounds!");
	return *(ppressure_ + N_*j + i);
}

bool Mac2d::is_solid(const int i, const int j){
	assert(i < N_ && j < M_ && "Index out of bounds!");
	return *(psolid_ + N_*j + i);
}

bool Mac2d::is_fluid(const int i, const int j){
	assert(i < N_ && j < M_ && "Index out of bounds!");
	return *(pfluid_ + N_*j + i);
}

bool Mac2d::is_empty(const int i, const int j){
	assert(i < N_ && j < M_ && "Index out of bounds!");
	return (!is_fluid(i,j) && !is_solid(i,j));
}

unsigned Mac2d::get_num_cells_x() {
	return N_;
}

unsigned Mac2d::get_num_cells_y() {
	return M_;
}

unsigned Mac2d::get_num_cells() {
	return M_*N_;
}

double Mac2d::get_cell_sizex() {
	return cell_sizex_;
}

double Mac2d::get_cell_sizey() {
	return cell_sizey_;
}

double Mac2d::get_weights_u(const int i, const int j){
	assert(i < (N_+1) && j < (M_) && "Index out of bounds!");
	return *(pweights_u_ + (N_+1)*j + i);
}

double Mac2d::get_weights_v(const int i, const int j){
	assert(i < (N_) && j < (M_+1) && "Index out of bounds!");
	return *(pweights_v_ + N_*j + i);
}

const std::vector< Mac2d::Triplet_t >& Mac2d::get_a_diag() {
	return A_diag_;
}

void Mac2d::set_u(const int i, const int j, double value){
	assert(i < (N_+1) && j < (M_) && "Index out of bounds!");
	*(pu_ + (N_+1)*j + i) = value;
}

void Mac2d::set_v(const int i, const int j, double value){
	assert(i < (N_) && j < (M_+1) && "Index out of bounds!");
	*(pv_ + N_*j + i) = value;
}

void Mac2d::set_pressure(const int i, const int j, double value){
	assert(i < N_ && j < M_ && "Index out of bounds!");
	*(ppressure_ + N_*j + i) = value;
}

void Mac2d::set_weights_u(const int i, const int j, double value){
	assert(i < (N_+1) && j < (M_) && "Index out of bounds!");
	*(pweights_u_ + (N_+1)*j + i) = value;
}

void Mac2d::set_weights_v(const int i, const int j, double value){
	assert(i < (N_) && j < (M_+1) && "Index out of bounds!");
	*(pweights_v_ + N_*j + i) = value;
}

void Mac2d::set_velocities_to_zero(){
	for( int j = 0; j < M_; ++j ){
		for( int i = 0; i < N_; ++i ){
			set_u(i, j, 0.);
			set_v(i, j, 0.);
		}	
	}
}

void Mac2d::set_weights_to_zero(){
	for( int j = 0; j < M_+1; ++j ){
		for( int i = 0; i < N_+1; ++i ){
			set_weights_u(i, j, 0.);
			set_weights_v(i, j, 0.);
		}	
	}
}

void Mac2d::set_pressure(const Eigen::VectorXd& p) {
	std::copy(p.data(), p.data()+p.size(), ppressure_);
}

void Mac2d::set_solid(const int i, const int j){
	assert(i < N_ && j < M_ && "Index out of bounds!");
	*(psolid_ + N_*j + i) = true;
}

void Mac2d::set_fluid(const int i, const int j){
	assert(i < N_ && j < M_ && "Index out of bounds!");
	*(pfluid_ + N_*j + i) = true;
}

void Mac2d::reset_fluid() {
	std::fill(pfluid_, pfluid_ + get_num_cells(), false);
}

Mac2d::Pair_t Mac2d::index_from_coord(const double x, const double y){
	assert((x > sizex_ || y > sizey_ || x < 0 || y < 0)
			&& "Attention: out of the grid!");
	return Pair_t(int(x), int(y));
}

//Useful function to print an array
/*std::ostream& operator<< (std::ostream& out, double* list, const int list_dimension){
	for(int i = 0; i < list_dimension; ++i){
		out << *(list + i) << std::endl;
	}
	return out;
}*/

