# SharpProspero shader programs

`mesh_vs.sb` and `mesh_ps.sb`, with the `.pssl` sources they were compiled
from, are taken unchanged from [SharpProspero](https://github.com/SvenGDK/SharpProspero)
by SvenGDK, `src/SharpProspero/Graphics/Agc/Shaders/`, under the GNU General
Public License version 3 (see `LICENSE`).

- `mesh_vs.sb` is the vertex program every draw uses.
- `mesh_ps.sb` is the starting point `shader_tools/agcpack.py texture-container`
  turns into the container the textured pixel programs are packed into.
