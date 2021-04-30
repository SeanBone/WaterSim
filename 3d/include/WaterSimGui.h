#ifndef WATERSIMGUI_H
#define WATERSIMGUI_H

#include "Simulation.h"
#include "FLIP.h"
#include "MeshExporter.h"
#include "SimConfig.h"
#include "WaterSim.h"

#include <sys/stat.h> // mkdir
#include <igl/opengl/glfw/Viewer.h>
#include <chrono> // accurate timings
#include <list> // std::list

/**
 * This class manages the water simulation in GUI mode.
 * It extends the functionality of WaterSim and handles rendering of
 * the visualization (particles & grid).
 */
class WaterSimGui : public Simulation {

protected:

	/*** Members relating to visualization ***/
	// Pointer to GLFW viewer used by Gui
	using viewer_t = igl::opengl::glfw::Viewer;
	viewer_t *const p_viewer;

	// Index of the ViewerData object containing particles for rendering
	//  in viewer.data_list
	unsigned int m_particles_data_idx;
	// Index of the ViewerData object containing the mesh representing
	//  the grid MAC grid in viewer.data_list
	unsigned int m_grid_data_idx;
	Eigen::MatrixXd m_particles; // Particle positions for rendering, Nx3
	Eigen::MatrixXd m_particle_colors; // Particle colours, Nx3

	Eigen::MatrixXd m_renderV;  // vertex positions for rendering, Nx3
	Eigen::MatrixXi m_renderE;  // MAC grid edges for rendering, Nx2
	Eigen::MatrixXi m_renderF;  // face indices for rendering, Nx3
	Eigen::MatrixXd m_renderC;  // colors per face for rendering, Nx3
	Eigen::MatrixXd m_renderEC; // colors of edges of mac grid, Nx3

public:

	/*** WaterSim instance */
	WaterSim m_watersim;

	/*** Public methods ***/

	/**
	 * Constructor
	 */
	WaterSimGui(viewer_t& viewer, const SimConfig& cfg, std::vector<bool> is_fluid);

	/**
	 * Reset class variables to reset the simulation.
	 */
	void resetMembers() override;

	/**
	 * Update simulation parameters. Requires a reset to take effect.
	 */
	void updateParams(const SimConfig& cfg, std::vector<bool> is_fluid);

	/**
	 * Update the rendering data structures. This method will be called in
	 * alternation with advance(). This method blocks rendering in the
	 * viewer, so do *not* do extensive computation here (leave it to
	 * advance()). This method need not be thread-safe with
	 * renderRenderGeometry(): mutual exclusion is guaranteed by Simulator.
	 */
	void updateRenderGeometry() override;

	/**
	 * Performs one simulation step of length m_dt. This method *must* be
	 * thread-safe with respect to renderRenderGeometry() (easiest is to not
	 * touch any rendering data structures at all). You have to update the time
	 * variables at the end of each step if they are necessary for your
	 * simulation.
	 */
	bool advance() override;

	/**
	 * Perform any actual rendering here. This method *must* be thread-safe with
	 * respect to advance(). This method runs in the same thread as the
	 * viewer and blocks user IO, so there really should not be any extensive
	 * computation here or the UI will lag/become unresponsive (the whole reason
	 * the simulation itself is in its own thread.)
	 */
	void renderRenderGeometry(igl::opengl::glfw::Viewer &viewer) override;

	void setTimestep(double t) override {
		Simulation::setTimestep(t);
		m_watersim.setTimestep(t);
	}

protected:

	/**
	 * Initialize a mesh to visualize the MAC grid
	 */
	void initMacViz();
};

#endif // WATERSIMGUI_H