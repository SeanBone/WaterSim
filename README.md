# meteor_pbs18

Meteorite crashing into the sea - Physically-Based Simulation HS18 project.

Authors: Silvia Nauer, Mikael Stellio, Sean Bone.

Slack channel: [https://flippingteamworkspace.slack.com](https://flippingteamworkspace.slack.com)

# Cloning with submodules
This Git repository has a submodule for libigl. To clone it correctly use either of the following:

    git clone --recurse-submodules git@gitlab.ethz.ch:bones/meteor_pbs18.git
    git clone --recurse-submodules https://gitlab.ethz.ch/bones/meteor_pbs18.git

# TODO
 - [X] Set up 2D visualization for FLIP particles & cells
 - [X] 2D FLIP working correctly
   - [X] MAC2D data structure
   - [X] 2D FLIP updates
 
 - [ ] Extend to 3D
   - [ ] 3D viz
   - [ ] MAC3D data structure
   - [ ] 3D FLIP updates
   - [ ] Signed distance function

 - [ ] Marching cubes & export mesh at each frame
 - [ ] Import meshes into Maya/Blender for rendering
