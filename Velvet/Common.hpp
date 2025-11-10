#pragma once

#include <glm/glm.hpp>
#include <imgui.h>
#include <functional>
#include <vector>

#define IMGUI_LEFT_LABEL(func, label, ...) (ImGui::TextUnformatted(label), ImGui::SameLine(), func("##" label, __VA_ARGS__))

// Only initialize value on host. 
// Since CUDA doesn't allow dynamics initialization, 
// we use this macro to ignore initialization when compiling with NVCC.
#ifdef __CUDA_ARCH__
	#define HOST_INIT(val) 
#else
	#define HOST_INIT(val) = val
#endif

struct VtSimParams
{
	int numSubsteps					HOST_INIT(2);
	int numIterations				HOST_INIT(4);						//!< Number of solver iterations to perform per-substep
	int maxNumNeighbors				HOST_INIT(64);
	float maxSpeed					HOST_INIT(50);						//!< The magnitude of particle velocity will be clamped to this value at the end of each step

	// Timing control
	int physicsFrameRate			HOST_INIT(60);						//!< Physics update frequency in FPS

	// forces
	glm::vec3 gravity				HOST_INIT(glm::vec3(0, -9.8f, 0));	//!< Constant acceleration applied to all particles
	
	// Wind system
	bool enableWind					HOST_INIT(false);					//!< Enable/disable wind effects
	glm::vec3 windDirection			HOST_INIT(glm::vec3(1, 0, 0));		//!< Wind direction vector (will be normalized)
	float windStrength				HOST_INIT(5.0f);					//!< Base wind strength
	float windTurbulence			HOST_INIT(0.3f);					//!< Random turbulence factor (0-1)
	float windFrequency				HOST_INIT(2.0f);					//!< Turbulence frequency
	
	float bendCompliance			HOST_INIT(10.0f);
	float damping					HOST_INIT(0.25f);					//!< Viscous drag force, applies a force proportional, and opposite to the particle velocity
	float relaxationFactor			HOST_INIT(1.0f);					//!< Control the convergence rate of the parallel solver, default: 1, values greater than 1 may lead to instability
	float longRangeStretchiness		HOST_INIT(1.2f);

	// collision
	float collisionMargin			HOST_INIT(0.06f);					//!< Distance particles maintain against shapes, note that for robust collision against triangle meshes this distance should be greater than zero
	float friction					HOST_INIT(0.1f);					//!< Coefficient of friction used when colliding against shapes
	bool enableSelfCollision		HOST_INIT(true);
	int interleavedHash				HOST_INIT(3);						//!< Hash once every n substeps. This can improves performance greatly.

	// runtime info
	unsigned int numParticles;											//!< Total number of particles 
	float particleDiameter;												//!< The maximum interaction radius for particles
	float deltaTime;	

	// misc
	float particleDiameterScalar	HOST_INIT(1.5f);					//!< multiply original stretch length by this scalar to obtain particle diameter
	float hashCellSizeScalar		HOST_INIT(1.5f);					//!< multiply particle diameter by this scalar to obtain hash cell size

	// Convergence Detection Parameters
	bool enableConvergenceCheck		HOST_INIT(false);				//!< Enable convergence detection
	float convergenceThreshold		HOST_INIT(0.01f);				//!< Constraint violation threshold (0-1, percentage)
	float positionChangeThreshold	HOST_INIT(1e-3f);				//!< Position change magnitude threshold (world space units)
	float velocityLimitRatio		HOST_INIT(0.1f);				//!< Velocity limit trigger ratio threshold (0-1, percentage of particles hitting velocity limit)
	
	// Iteration Stability Detection Parameters
	bool enableIterationStability	HOST_INIT(false);				//!< Enable iteration-to-iteration position change detection
	float iterationChangeThreshold	HOST_INIT(1e-4f);				//!< Maximum allowed position change between iterations (world space units)
	int requiredStableIterations	HOST_INIT(3);					//!< Number of consecutive stable iterations required for stability
	bool enableEarlyExit			HOST_INIT(true);				//!< Enable early exit when stability is achieved
	
	// Convergence Detection Runtime Metrics (Read-only, updated by solver)
	float avgConstraintViolation	HOST_INIT(0.0f);				//!< Current average constraint violation
	float avgPositionChange			HOST_INIT(0.0f);				//!< Current average position change magnitude  
	float velocityLimitTriggerRatio	HOST_INIT(0.0f);				//!< Current velocity limit trigger ratio
	bool isConverged				HOST_INIT(false);				//!< Current convergence state
	int convergenceFrameCount		HOST_INIT(0);					//!< Consecutive converged frame count
	
	// Iteration Stability Runtime Metrics (Read-only, updated by solver)
	float avgIterationChange		HOST_INIT(0.0f);				//!< Average position change between iterations
	float maxIterationChange		HOST_INIT(0.0f);				//!< Maximum position change between iterations
	bool isIterationStable			HOST_INIT(false);				//!< Current iteration stability state
	int stableIterationCount		HOST_INIT(0);					//!< Count of consecutive stable iterations in current substep
	int actualIterationsUsed		HOST_INIT(0);					//!< Actual iterations used (may be less than max due to early exit)

	void OnGUI();  // Declaration moved to separate cpp file for implementation
};

struct VtGameState
{
	bool step = false;
	bool pause = false;
	bool renderWireframe = false;
	bool drawParticles = false;
	bool hideGUI = false;
	bool detailTimer = false;
};

template <class T, class... TArgs>
class VtCallback
{
public:
	void Register(const std::function<T>& func)
	{
		m_funcs.push_back(func);
	}

	template <class... TArgs>
	void Invoke(TArgs... args)
	{
		for (const auto& func : m_funcs)
		{
			func(std::forward<TArgs>(args)...);
		}
	}

	void Clear()
	{
		m_funcs.clear();
	}

	bool empty()
	{
		return m_funcs.size() == 0;
	}

private:
	std::vector<std::function<T>> m_funcs;
};

enum class ColliderType
{
	Sphere,
	Plane,
	Cube,
};