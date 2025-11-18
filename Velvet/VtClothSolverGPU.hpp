#pragma once

#include <iostream>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <thrust/device_ptr.h>
#include <thrust/transform.h>
#include <deque>

#include "helper_cuda.h"
#include "Mesh.hpp"
#include "VtClothSolverGPU.cuh"
#include "VtBuffer.hpp"
#include "SpatialHashGPU.hpp"
#include "MouseGrabber.hpp"

using namespace std;

namespace Velvet
{
	class VtClothSolverGPU : public Component
	{
	public:

		void Start() override
		{
			Global::simParams.numParticles = 0;
			m_colliders = Global::game->FindComponents<Collider>();
			m_mouseGrabber.Initialize(&positions, &velocities, &invMasses);
			
			// Initialize convergence detection - always initialize, even if disabled
			InitializeConvergenceDetection();
			ShowDebugGUI(); // Enable debug GUI to inspect distance-based weights
		}

		void Update() override
		{
			m_mouseGrabber.HandleMouseInteraction();
		}

		void FixedUpdate() override
		{
			m_mouseGrabber.UpdateGrappedVertex();
			UpdateColliders(m_colliders);

			Timer::StartTimer("GPU_TIME");
			Simulate();
			Timer::EndTimer("GPU_TIME");
		}

		void OnDestroy() override
		{
			positions.destroy();
			normals.destroy();
			
			// Cleanup convergence detection resources
			CleanupConvergenceDetection();
		}

		void Simulate()
		{
			Timer::StartTimerGPU("Solver_Total");
			//==========================
			// Prepare
			//==========================
			float frameTime = Timer::fixedDeltaTime();
			float substepTime = Timer::fixedDeltaTime() / Global::simParams.numSubsteps;

			//==========================
			// Launch kernel
			//==========================
			SetSimulationParams(&Global::simParams);

			// ?? PERFORMANCE OPTIMIZATION: Distance-based weights are now computed ONCE during initialization
			// and cached, instead of computing every frame. This provides massive performance improvement!
			// The distances are based on initial positions relative to fixed points and remain constant.

			// External colliders can move relatively fast, and cloth will have large velocity after colliding with them.
			// This can produce unstable behavior, such as vertex flashing between two sides.
			// We include a pre-stabilization step to mitigate this issue. Collision here will not influence velocity.
			CollideSDF(positions, sdfColliders, positions, (uint)sdfColliders.size(), frameTime);

			for (int substep = 0; substep < Global::simParams.numSubsteps; substep++)
			{
				PredictPositions(predicted, velocities, positions, substepTime);
				 
				if (Global::simParams.enableSelfCollision)
				{
					if (substep % Global::simParams.interleavedHash == 0)
					{
						m_spatialHash->Hash(predicted);
					}
					CollideParticles(deltas, deltaCounts, predicted, invMasses, m_spatialHash->neighbors, positions);
				}
				CollideSDF(predicted, sdfColliders, positions, (uint)sdfColliders.size(), substepTime);

				// Iteration stability detection for this substep
				int stableIterationCount = 0;
				float totalIterationChange = 0.0f;
				float maxIterationChange = 0.0f;
				int actualIterations = 0;

				// Initialize previous positions for iteration stability tracking
				VtBuffer<glm::vec3> previousIterationPositions;
				if (Global::simParams.enableIterationStability)
				{
					previousIterationPositions.resize(Global::simParams.numParticles);
					// Copy initial predicted positions as baseline
					cudaMemcpy(previousIterationPositions.data(), (glm::vec3*)predicted, 
						Global::simParams.numParticles * sizeof(glm::vec3), cudaMemcpyDeviceToDevice);
				}

				for (int iteration = 0; iteration < Global::simParams.numIterations; iteration++)
				{
					actualIterations = iteration + 1;

					SolveStretch(predicted, deltas, deltaCounts, stretchIndices, stretchLengths, invMasses, distancesToFixedPoints, (uint)stretchLengths.size());
					SolveAttachment(predicted, deltas, deltaCounts, invMasses,
						attachParticleIDs, attachSlotIDs, attachSlotPositions, attachDistances, (uint)attachParticleIDs.size());
					//SolveBending(predicted, deltas, deltaCounts, bendIndices, bendAngles, invMasses, (uint)bendAngles.size(), substepTime);
					ApplyDeltas(predicted, deltas, deltaCounts);

					// Check iteration stability if enabled
					if (Global::simParams.enableIterationStability)
					{
						float avgChange, maxChange;
						ComputeIterationStabilityMetrics(
							&avgChange, &maxChange,
							previousIterationPositions.data(),
							(glm::vec3*)predicted,
							Global::simParams.numParticles
						);

						totalIterationChange += avgChange;
						maxIterationChange = max(maxIterationChange, maxChange);

						// Update previous positions for next iteration
						cudaMemcpy(previousIterationPositions.data(), (glm::vec3*)predicted, 
							Global::simParams.numParticles * sizeof(glm::vec3), cudaMemcpyDeviceToDevice);

						// Check if this iteration is stable
						if (avgChange <= Global::simParams.iterationChangeThreshold)
						{
							stableIterationCount++;
						}
						else
						{
							stableIterationCount = 0; // Reset counter on unstable iteration
						}

						// Early exit if we've achieved required stable iterations and early exit is enabled
						if (Global::simParams.enableEarlyExit && 
							stableIterationCount >= Global::simParams.requiredStableIterations)
						{
							break; // Early convergence, exit iteration loop
						}
					}
				}

				// Update iteration stability metrics for this substep
				if (Global::simParams.enableIterationStability)
				{
					Global::simParams.avgIterationChange = (actualIterations > 0) ? 
						totalIterationChange / actualIterations : 0.0f;
					Global::simParams.maxIterationChange = maxIterationChange;
					Global::simParams.stableIterationCount = stableIterationCount;
					Global::simParams.isIterationStable = (stableIterationCount >= Global::simParams.requiredStableIterations);
					Global::simParams.actualIterationsUsed = actualIterations;
				}
				else
				{
					// Set default values when iteration stability is disabled
					Global::simParams.avgIterationChange = 0.0f;
					Global::simParams.maxIterationChange = 0.0f;
					Global::simParams.stableIterationCount = 0;
					Global::simParams.isIterationStable = false;
					Global::simParams.actualIterationsUsed = actualIterations;
				}

				Finalize(velocities, positions, predicted, substepTime);
			}

			ComputeNormal(normals, positions, indices, (uint)(indices.size() / 3));

			// Convergence Detection - MOVED AFTER Finalize to get correct position and velocity data
			if (Global::simParams.enableConvergenceCheck)
			{
				UpdateConvergenceMetrics();
			}

			//==========================
			// Sync
			//==========================
			Timer::EndTimerGPU("Solver_Total");
			cudaDeviceSynchronize();

			positions.sync();
			normals.sync();
		}

		int AddCloth(shared_ptr<Mesh> mesh, glm::mat4 modelMatrix, float particleDiameter)
		{
			Timer::StartTimer("INIT_SOLVER_GPU");

			int prevNumParticles = Global::simParams.numParticles;
			int newParticles = (int)mesh->vertices().size();

			// Set global parameters
			Global::simParams.numParticles += newParticles;
			Global::simParams.particleDiameter = particleDiameter;
			Global::simParams.deltaTime = Timer::fixedDeltaTime();
			Global::simParams.maxSpeed = 2 * particleDiameter / Timer::fixedDeltaTime() * Global::simParams.numSubsteps;

			// Allocate managed buffers
			positions.registerNewBuffer(mesh->verticesVBO());
			normals.registerNewBuffer(mesh->normalsVBO());

			for (int i = 0; i < mesh->indices().size(); i++)
			{
				indices.push_back(mesh->indices()[i] + prevNumParticles);
			}

			velocities.push_back(newParticles, glm::vec3(0));
			predicted.push_back(newParticles, glm::vec3(0));
			deltas.push_back(newParticles, glm::vec3(0));
			deltaCounts.push_back(newParticles, 0);
			invMasses.push_back(newParticles, 1.0f);

			 // ?? FIXED: Initialize distance-based weight buffers with 1.0 (max normalized distance) as default
			 // This ensures that if distance weights are not computed, particles behave as if they're far from fixed points
			distancesToFixedPoints.push_back(newParticles, 1.0f);

			// Initialize buffer datas
			InitializePositions(positions, prevNumParticles, newParticles, modelMatrix);
			cudaDeviceSynchronize();
			positions.sync();

			// Initialize member variables
			m_spatialHash = make_shared<SpatialHashGPU>(particleDiameter, Global::simParams.numParticles);
			m_spatialHash->SetInitialPositions(positions);

			double time = Timer::EndTimer("INIT_SOLVER_GPU") * 1000;
			fmt::print("Info(ClothSolverGPU): AddCloth done. Took time {:.2f} ms\n", time);
			fmt::print("Info(ClothSolverGPU): Use recommond max vel = {}\n", Global::simParams.maxSpeed);

			return prevNumParticles;
		}

		 // ?? UPDATED FUNCTION: Initialize distance-based weights with automatic max distance calculation
		void InitializeDistanceBasedWeights()
		{
			printf("Info(DistanceWeights): InitializeDistanceBasedWeights called...\n");
			printf("Info(DistanceWeights): useDistanceBasedWeights = %s\n", Global::simParams.useDistanceBasedWeights ? "TRUE" : "FALSE");
			printf("Info(DistanceWeights): actualFixedPoints.size() = %d\n", (int)actualFixedPoints.size());
			
			if (!Global::simParams.useDistanceBasedWeights || actualFixedPoints.size() == 0) 
			{
				printf("Info(DistanceWeights): Distance-based weights disabled or no fixed points registered\n");
				printf("Info(DistanceWeights): All particles will use default distance value 1.0\n");
				return;
			}

			Timer::StartTimer("INIT_DISTANCE_WEIGHTS");
			
			printf("Info(DistanceWeights): Computing distance-based weights with AUTO max distance calculation...\n");
			printf("Info(DistanceWeights): Particles: %d, Fixed Points: %d\n", 
				Global::simParams.numParticles, (int)actualFixedPoints.size());

			 // ?? UPDATED: Auto-calculate max distance and normalize all distances
			ComputeDistancesToActualFixedPoints(
				distancesToFixedPoints.data(),
				(glm::vec3*)positions,
				actualFixedPoints.data(),
				Global::simParams.numParticles,
				(uint)actualFixedPoints.size(),
				0.0f); // maxDistance parameter is no longer used

			cudaDeviceSynchronize(); // Ensure computation is complete

			double time = Timer::EndTimer("INIT_DISTANCE_WEIGHTS") * 1000;
			printf("Info(DistanceWeights): Distance-based weights initialized in %.2f ms\n", time);
			printf("Info(DistanceWeights): ? NO manual configuration needed - distances AUTO-NORMALIZED!\n");
			printf("Info(DistanceWeights): Weights are CACHED and will NOT be recomputed every frame!\n");
		}

		void AddStretch(int idx1, int idx2, float distance)
		{
			stretchIndices.push_back(idx1);
			stretchIndices.push_back(idx2);
			stretchLengths.push_back(distance);
		}

		void AddAttachSlot(glm::vec3 attachSlotPos)
		{
			attachSlotPositions.push_back(attachSlotPos);
		}

		void AddAttach(int particleIndex, int slotIndex, float distance)
		{
			if (distance == 0) invMasses[particleIndex] = 0;
			attachParticleIDs.push_back(particleIndex);
			attachSlotIDs.push_back(slotIndex);
			attachDistances.push_back(distance);
		}

		void AddBend(uint idx1, uint idx2, uint idx3, uint idx4, float angle)
		{
			bendIndices.push_back(idx1);
			bendIndices.push_back(idx2);
			bendIndices.push_back(idx3);
			bendIndices.push_back(idx4);
			bendAngles.push_back(angle);
		}

		 // ?? NEW: Register an actual fixed point for distance-based weight calculation
		void RegisterFixedPoint(glm::vec3 fixedPos)
		{
			actualFixedPoints.push_back(fixedPos);
			printf("Info(ClothSolver): Registered fixed point at (%.2f, %.2f, %.2f) - Total: %d\n", 
				fixedPos.x, fixedPos.y, fixedPos.z, (int)actualFixedPoints.size());
		}

		void UpdateColliders(vector<Collider*>& colliders)
		{
			sdfColliders.resize(colliders.size());

			for (int i = 0; i < colliders.size(); i++)
			{
				const Collider* c = colliders[i];
				if (!c->enabled) continue;
				SDFCollider sc;
				sc.type = c->type;
				sc.position = c->actor->transform->position;
				sc.scale = c->actor->transform->scale;
				sc.curTransform = c->curTransform;
				sc.invCurTransform = glm::inverse(c->curTransform);
				sc.lastTransform = c->lastTransform;
				sc.deltaTime = Timer::fixedDeltaTime();
				sdfColliders[i] = sc; 
			}
		}

	public: // Sim buffers

		VtMergedBuffer<glm::vec3> positions;
		VtMergedBuffer<glm::vec3> normals;
		VtBuffer<uint> indices;

		VtBuffer<glm::vec3> velocities;
		VtBuffer<glm::vec3> predicted;
		VtBuffer<glm::vec3> deltas;
		VtBuffer<int> deltaCounts;
		VtBuffer<float> invMasses;

		// Distance-based weight system buffers
		VtBuffer<float> distancesToFixedPoints;		// Distance from each vertex to nearest fixed point
		VtBuffer<glm::vec3> actualFixedPoints;		// ?? NEW: Store actual fixed point positions directly

		VtBuffer<int> stretchIndices;
		VtBuffer<float> stretchLengths;
		VtBuffer<uint> bendIndices;
		VtBuffer<float> bendAngles;

		// Attach attachParticleIndices[i] with attachSlotIndices[i] w
		// where their expected distance is attachDistances[i]
		VtBuffer<int> attachParticleIDs;
		VtBuffer<int> attachSlotIDs;
		VtBuffer<float> attachDistances;
		VtBuffer<glm::vec3> attachSlotPositions;

		VtBuffer<SDFCollider> sdfColliders;

	private:

		shared_ptr<SpatialHashGPU> m_spatialHash;
		vector<Collider*> m_colliders;
		MouseGrabber m_mouseGrabber;

		// Convergence Detection Members
		ConvergenceMetrics* d_convergenceMetrics = nullptr; // Host memory for convergence metrics
		static constexpr int CONVERGENCE_HISTORY_SIZE = 30;
		deque<float> m_constraintViolationHistory;
		deque<float> m_positionChangeHistory;
		deque<float> m_velocityLimitRatioHistory;
		int m_convergenceFrameCount = 0;

		void InitializeConvergenceDetection()
		{
			// Allocate host memory for convergence metrics
			if (!d_convergenceMetrics)
			{
				d_convergenceMetrics = new ConvergenceMetrics();
				memset(d_convergenceMetrics, 0, sizeof(ConvergenceMetrics));
			}
		}

		void CleanupConvergenceDetection()
		{
			if (d_convergenceMetrics)
			{
				delete d_convergenceMetrics;
				d_convergenceMetrics = nullptr;
			}
		}

		void UpdateConvergenceMetrics()
		{
			if (!Global::simParams.enableConvergenceCheck || !d_convergenceMetrics) return;
			
			// Make sure we have constraints to check
			if (stretchLengths.size() == 0)
			{
				// No constraints, set metrics to zero
				Global::simParams.avgConstraintViolation = 0.0f;
				Global::simParams.avgPositionChange = 0.0f;
				Global::simParams.velocityLimitTriggerRatio = 0.0f;
				Global::simParams.isConverged = true;
				Global::simParams.convergenceFrameCount = 0;
				return;
			}

			// Store previous positions before calling convergence check
			static VtBuffer<glm::vec3> previousPositions;
			if (previousPositions.size() != positions.size())
			{
				previousPositions.resize(positions.size());
				// Copy current positions as "previous" on first run
				cudaMemcpy(previousPositions.data(), (glm::vec3*)positions, 
					positions.size() * sizeof(glm::vec3), cudaMemcpyDeviceToDevice);
			}

			// Compute convergence metrics on GPU using previous positions vs current positions
			ComputeConvergenceMetrics(
				d_convergenceMetrics,
				previousPositions.data(),  // Use previous positions for comparison
				(glm::vec3*)positions,     // Current positions (after Finalize)
				velocities.data(),         // Current velocities
				stretchIndices.data(), stretchLengths.data(),
				Global::simParams.numParticles,
				(uint)stretchLengths.size(),
				Global::simParams.maxSpeed,
				Timer::fixedDeltaTime());

			// Update previous positions for next frame
			cudaMemcpy(previousPositions.data(), (glm::vec3*)positions, 
				positions.size() * sizeof(glm::vec3), cudaMemcpyDeviceToDevice);

			// Calculate averages
			float avgConstraintViolation = 0.0f;
			float avgPositionChange = 0.0f;
			float velocityLimitTriggerRatio = 0.0f;

			if (d_convergenceMetrics->totalParticleCount > 0)
			{
				if (stretchLengths.size() > 0)
				{
					avgConstraintViolation = d_convergenceMetrics->totalConstraintViolation / (float)stretchLengths.size();
				}
				avgPositionChange = d_convergenceMetrics->totalPositionChange / (float)d_convergenceMetrics->totalParticleCount;
				velocityLimitTriggerRatio = (float)d_convergenceMetrics->velocityLimitCount / (float)d_convergenceMetrics->totalParticleCount;
			}

			// Update history
			m_constraintViolationHistory.push_back(avgConstraintViolation);
			m_positionChangeHistory.push_back(avgPositionChange);
			m_velocityLimitRatioHistory.push_back(velocityLimitTriggerRatio);

			// Limit history size
			if (m_constraintViolationHistory.size() > CONVERGENCE_HISTORY_SIZE)
			{
				m_constraintViolationHistory.pop_front();
			}
			if (m_positionChangeHistory.size() > CONVERGENCE_HISTORY_SIZE)
			{
				m_positionChangeHistory.pop_front();
			}
			if (m_velocityLimitRatioHistory.size() > CONVERGENCE_HISTORY_SIZE)
			{
				m_velocityLimitRatioHistory.pop_front();
			}

			// Check convergence
			bool isConverged = CheckConvergence(avgConstraintViolation, avgPositionChange, velocityLimitTriggerRatio);

			// Update convergence frame count
			if (isConverged)
			{
				m_convergenceFrameCount++;
			}
			else
			{
				m_convergenceFrameCount = 0;
			}

			// Update global parameters
			Global::simParams.avgConstraintViolation = avgConstraintViolation;
			Global::simParams.avgPositionChange = avgPositionChange;
			Global::simParams.velocityLimitTriggerRatio = velocityLimitTriggerRatio;
			Global::simParams.isConverged = isConverged;
			Global::simParams.convergenceFrameCount = m_convergenceFrameCount;
			
			// Debug output (can be enabled for debugging)
			static int debugCounter = 0;
			if (++debugCounter % 60 == 0) // Print every 60 frames
			{
				printf("Host Debug: Violations=%.6f, PosChange=%.6f, VelRatio=%.3f%%, Converged=%s\n",
					avgConstraintViolation, avgPositionChange, velocityLimitTriggerRatio * 100.0f,
					isConverged ? "YES" : "NO");
			}
		}

		bool CheckConvergence(float avgConstraintViolation, float avgPositionChange, float velocityLimitTriggerRatio)
		{
			bool constraintConverged = avgConstraintViolation <= Global::simParams.convergenceThreshold;
			bool positionConverged = avgPositionChange <= Global::simParams.positionChangeThreshold;
			bool velocityConverged = velocityLimitTriggerRatio <= Global::simParams.velocityLimitRatio;
			
			return constraintConverged && positionConverged && velocityConverged;
		}

		void ShowDebugGUI()
		{
			GUI::RegisterDebug([this]() {
				{
					static int particleIndex1 = 0;
					//IMGUI_LEFT_LABEL(ImGui::InputInt, "ParticleID", &particleIndex, 0, m_numParticles-1);
					IMGUI_LEFT_LABEL(ImGui::SliderInt, "ParticleID1", &particleIndex1, 0, Global::simParams.numParticles - 1);
					ImGui::Indent(10);
					ImGui::Text(fmt::format("Position: {}", predicted[particleIndex1]).c_str());
					auto hash3i = m_spatialHash->HashPosition3i(predicted[particleIndex1]);
					auto hash = m_spatialHash->HashPosition(predicted[particleIndex1]);
					ImGui::Text(fmt::format("Hash: {}[{},{},{}]", hash, hash3i.x, hash3i.y, hash3i.z).c_str());
					auto norm = normals[particleIndex1];
					ImGui::Text(fmt::format("Normal: [{:.3f},{:.3f},{:.3f}]", norm.x, norm.y, norm.z).c_str());

					// Add distance-based weight debugging
					if (Global::simParams.useDistanceBasedWeights && distancesToFixedPoints.size() > particleIndex1)
					{
						float normalizedDist = distancesToFixedPoints[particleIndex1];
						ImGui::Text(fmt::format("Normalized Distance: {:.3f}", normalizedDist).c_str());
						float weight = ComputeDistanceWeightHost(normalizedDist, 1.0f, Global::simParams.distanceWeightFalloff);
						ImGui::Text(fmt::format("Computed Weight: {:.3f}", weight).c_str());
					}
					else
					{
						ImGui::Text("Distance-based weights: DISABLED or no data");
					}

					static int neighborRange1 = 0;
					IMGUI_LEFT_LABEL(ImGui::SliderInt, "NeighborRange1", &neighborRange1, 0, 63);
					ImGui::Text(fmt::format("NeighborID: {}", m_spatialHash->neighbors[neighborRange1 + particleIndex1 * Global::simParams.maxNumNeighbors]).c_str());
					ImGui::Indent(-10);
				}

				{
					static int particleIndex2 = 0;
					//IMGUI_LEFT_LABEL(ImGui::InputInt, "ParticleID", &particleIndex, 0, m_numParticles-1);
					IMGUI_LEFT_LABEL(ImGui::SliderInt, "ParticleID2", &particleIndex2, 0, Global::simParams.numParticles - 1);
					ImGui::Indent(10);
					ImGui::Text(fmt::format("Position: {}", predicted[particleIndex2]).c_str());
					auto hash3i = m_spatialHash->HashPosition3i(predicted[particleIndex2]);
					auto hash = m_spatialHash->HashPosition(predicted[particleIndex2]);
					ImGui::Text(fmt::format("Hash: {}[{},{},{}]", hash, hash3i.x, hash3i.y, hash3i.z).c_str());

					static int neighborRange2 = 0;
					IMGUI_LEFT_LABEL(ImGui::SliderInt, "NeighborRange2", &neighborRange2, 0, 63);
					ImGui::Text(fmt::format("NeighborID: {}", m_spatialHash->neighbors[neighborRange2 + particleIndex2 * Global::simParams.maxNumNeighbors]).c_str());
					ImGui::Indent(-10);
				}

				// Distance-based weight system debugging section
				if (ImGui::CollapsingHeader("Distance-Based Weights Debug"))
				{
					ImGui::Text("System Status: %s", Global::simParams.useDistanceBasedWeights ? "ENABLED" : "DISABLED");
					ImGui::Text("?? Max Distance: AUTO-CALCULATED (no manual config needed!)");
					ImGui::Text("Distance Weight Falloff: %.2f", Global::simParams.distanceWeightFalloff);
					ImGui::Text("Attachment Points Count: %d", (int)attachParticleIDs.size());
					ImGui::Text("Distance Buffer Size: %d", (int)distancesToFixedPoints.size());
					ImGui::Text("?? All distances automatically normalized to [0,1] range");
					
					if (attachParticleIDs.size() > 0)
					{
						ImGui::Text("Attached Particle IDs:");
						ImGui::Indent(15);
						for (int i = 0; i < min(10, (int)attachParticleIDs.size()); i++)
						{
							int slotIndex = attachSlotIDs[i];
							glm::vec3 slotPos = attachSlotPositions[slotIndex];
							ImGui::Text("  [%d] -> Particle %d, Slot %d, Pos(%.2f,%.2f,%.2f)", 
								i, attachParticleIDs[i], slotIndex, slotPos.x, slotPos.y, slotPos.z);
						}
						if (attachParticleIDs.size() > 10)
						{
							ImGui::Text("  ... and %d more", (int)attachParticleIDs.size() - 10);
						}
						ImGui::Indent(-15);
					}

					// Show some sample distance values (now normalized)
					if (distancesToFixedPoints.size() > 0 && Global::simParams.useDistanceBasedWeights)
					{
						ImGui::Text("Sample Normalized Distance Values:");
						ImGui::Indent(15);
						int sampleCount = min(10, (int)distancesToFixedPoints.size());
						for (int i = 0; i < sampleCount; i++)
						{
							int idx = i * (int)distancesToFixedPoints.size() / sampleCount;
							float normalizedDistance = distancesToFixedPoints[idx];
							float weight = ComputeDistanceWeightHost(normalizedDistance, 1.0f, Global::simParams.distanceWeightFalloff);
							ImGui::Text("  Particle %d: NormDist=%.3f, Weight=%.3f", idx, normalizedDistance, weight);
						}
						ImGui::Indent(-15);
					}
				}

				static int cellID = 0;
				IMGUI_LEFT_LABEL(ImGui::SliderInt, "CellID", &cellID, 0, (int)m_spatialHash->cellStart.size() - 1);
				int start = m_spatialHash->cellStart[cellID];
				int end = m_spatialHash->cellEnd[cellID];
				ImGui::Indent(10);
				ImGui::Text(fmt::format("CellStart.HashID: {}", start).c_str());
				ImGui::Text(fmt::format("CellEnd.HashID: {}", end).c_str());

				if (start != 0xffffffff && end > start)
				{
					static int particleHash = 0;
					particleHash = clamp(particleHash, start, end - 1);
					IMGUI_LEFT_LABEL(ImGui::SliderInt, "HashID", &particleHash, start, end - 1);
					ImGui::Text(fmt::format("ParticleHash: {}", m_spatialHash->particleHash[particleHash]).c_str());
					ImGui::Text(fmt::format("ParticleIndex: {}", m_spatialHash->particleIndex[particleHash]).c_str());
				}
				});
		}

	private:
		// Host-side weight calculation for debugging (updated for normalized distances)
		float ComputeDistanceWeightHost(float normalizedDistance, float maxDistance, float falloff) const
		{
			// ?? UPDATED: Distance is pre-normalized to [0,1] range
			// normalizedDistance: 0 = at fixed point, 1 = farthest from any fixed point
			
			// Invert the distance so closer points get higher weights
			// Apply falloff - higher falloff means more sharp transition
			float weight = powf(1.0f - normalizedDistance, falloff);
			
			// Ensure minimum weight to avoid overly rigid behavior, but allow higher maximum
			return max(weight, 0.01f);
		}
	};
}