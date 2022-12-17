# vierkant_projects
Project repository for applications built with the vierkant rendering framework

https://github.com/crocdialer/vierkant

- pbr_viewer ![Preview](https://crocdialer.com/wp-content/uploads/2022/10/2022-09-30-chessboard.jpg)
  - small 3d-viewer with a raster- and raytracing backends
  - ![example workflow](https://github.com/crocdialer/vierkant_projects/actions/workflows/cmake_build.yml/badge.svg)
  
dependencies:
-
`sudo apt install libboost-all-dev libcurl4-openssl-dev xorg-dev vulkan-sdk`

checkout & build:
-
```
git clone git@github.com:crocdialer/vierkant_projects.git --recurse-submodules
mkdir -p vierkant_projects/build
cd vierkant_projects/build
cmake .. && make -jX
```

usage:
-

- view a gltf2-model using an HDR-background:
```
./pbr_viewer foo.gltf bar.hdr
```

- drag&drop model/image-files
- save settings: 's'