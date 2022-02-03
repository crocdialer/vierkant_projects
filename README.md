# vierkant_projects
Project repository for applications built with the vierkant rendering framework

https://github.com/crocdialer/vierkant

- pbr_viewer
  - small 3d-viewer with a raster- and raytracing backend

dependencies:
-
`sudo apt install libboost-all-dev libcurl4-openssl-dev xorg-dev vulkan-sdk`

checkout & build:
-
`git clone git@github.com:crocdialer/vierkant_projects.git --recurse-submodules`

`mkdir -p vierkant_projects/build`

`cd vierkant_projects/build`

`cmake .. && make -j8`
