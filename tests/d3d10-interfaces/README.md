# D3D10 interfaces on D3D11 resources

`d3d10_interfaces.cpp` checks that a texture from a D3D11 device refuses
`ID3D10Texture2D` and `ID3D10Resource` (as on Windows) while a texture from a
device created through `D3D10CoreCreateDevice` still exposes them. Build with
mingw-w64 (`-ld3d11 -ldxgi`) and run under Wine with DXMT's `d3d11.dll` and
`d3d10core.dll`. On 2026-10-14 the previous build answered both D3D10
interfaces on the D3D11 texture (FAIL); with the change it returns
E_NOINTERFACE, and the D3D10 texture still answers them.
