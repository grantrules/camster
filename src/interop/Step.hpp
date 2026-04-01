#pragma once

#include <string>

#include "StlMesh.hpp"

bool exportStepMesh(const std::string& path, const StlMesh& mesh, std::string& error);
bool importStepMesh(const std::string& path, StlMesh& mesh, std::string& error);
