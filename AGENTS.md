# Project instructions

- Write project automation and scripts in C# with XML documentation comments
  (XDoc). Avoid Python for custom tooling.
- Sphinx is the required documentation system. Its minimal `docs/conf.py` and
  third-party Python runtime are the documentation-only exception; do not add
  custom Python build, test, or maintenance scripts.
- Use CMake/Ninja for native/firmware builds and .NET for host tooling. Keep
  kernel and driver storage static at boot and preserve the no-heap audit.
