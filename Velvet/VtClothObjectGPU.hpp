#pragma once

#include "Component.hpp"
#include "VtClothSolverGPU.hpp"
#include "Actor.hpp"
#include "MeshRenderer.hpp"
#include "VtEngine.hpp"

namespace Velvet
{
	class VtClothObjectGPU : public Component
	{
	public:
		VtClothObjectGPU(int resolution, shared_ptr<VtClothSolverGPU> solver)
		{
			SET_COMPONENT_NAME;

			m_solver = solver;
			m_resolution = resolution;
		}

		void SetAttachedIndices(vector<int> indices)
		{
			m_attachedIndices = indices;
		}

		auto particleDiameter() const
		{
			return m_particleDiameter;
		}

		auto solver() const
		{
			return m_solver;
		}

		VtBuffer<glm::vec3> &attachSlotPositions() const
		{
			return m_solver->attachSlotPositions;
		}

	public:
		void Start() override
		{
			auto mesh = actor->GetComponent<MeshRenderer>()->mesh();
			auto transformMatrix = actor->transform->matrix();
			auto positions = mesh->vertices();
			auto indices = mesh->indices();
			m_particleDiameter = glm::length(positions[0] - positions[1]) * Global::simParams.particleDiameterScalar;
			std::cout << "Particle diameter: " << m_particleDiameter << std::endl;

			m_indexOffset = m_solver->AddCloth(mesh, transformMatrix, m_particleDiameter);
			actor->transform->Reset();

			ApplyTransform(positions, transformMatrix);
			GenerateStretch(positions);
			GenerateAttach(positions);
			GenerateBending(indices);
			
			// ?? IMPORTANT: Initialize distance-based weights ONCE after all constraints are set up
			// This computes distances from each particle to fixed points using initial positions
			// and caches them for the entire simulation duration - massive performance improvement!
			m_solver->InitializeDistanceBasedWeights();
		}

	private:
		int m_resolution;
		int m_indexOffset;
		shared_ptr<VtClothSolverGPU> m_solver;
		vector<int> m_attachedIndices;
		float m_particleDiameter;

		void ApplyTransform(vector<glm::vec3>& positions, glm::mat4 transform)
		{
			for (int i = 0; i < positions.size(); i++)
			{
				positions[i] = transform * glm::vec4(positions[i], 1.0);
			}
		}

		void GenerateStretch(const vector<glm::vec3> &positions)
		{
			auto VertexAt = [this](int x, int y) {
				return x * (m_resolution + 1) + y;
			};
			auto DistanceBetween = [&positions](int idx1, int idx2) {
				return glm::length(positions[idx1] - positions[idx2]);
			};

			for (int x = 0; x < m_resolution + 1; x++)
			{
				for (int y = 0; y < m_resolution + 1; y++)
				{
					int idx1, idx2;

					if (y != m_resolution)
					{
						idx1 = VertexAt(x, y);
						idx2 = VertexAt(x, y + 1);
						m_solver->AddStretch(m_indexOffset + idx1, m_indexOffset + idx2, DistanceBetween(idx1, idx2));
					}

					if (x != m_resolution)
					{
						idx1 = VertexAt(x, y);
						idx2 = VertexAt(x + 1, y);
						m_solver->AddStretch(m_indexOffset + idx1, m_indexOffset + idx2, DistanceBetween(idx1, idx2));
					}

					if (y != m_resolution && x != m_resolution)
					{
						idx1 = VertexAt(x, y);
						idx2 = VertexAt(x + 1, y + 1);
						m_solver->AddStretch(m_indexOffset + idx1, m_indexOffset + idx2, DistanceBetween(idx1, idx2));

						idx1 = VertexAt(x, y + 1);
						idx2 = VertexAt(x + 1, y);
						m_solver->AddStretch(m_indexOffset + idx1, m_indexOffset + idx2, DistanceBetween(idx1, idx2));
					}
				}
			}
		}
	
		void GenerateBending(const vector<unsigned int>& indices)
		{
			// HACK: not for every kind of mesh
			for (int i = 0; i < indices.size(); i += 6)
			{
				int idx1 = indices[i];
				int idx2 = indices[i + 1];
				int idx3 = indices[i + 2];
				int idx4 = indices[i + 5];

				// TODO: calculate angle
				float angle = 0;
				m_solver->AddBend(m_indexOffset + idx1, m_indexOffset + idx2, m_indexOffset + idx3, m_indexOffset + idx4, angle);
			}
		}

		void GenerateAttach(const vector<glm::vec3>& positions)
		{
			// ?? FIXED: Restore distance constraint system while properly registering fixed points
			for (int slotIdx = 0; slotIdx < m_attachedIndices.size(); slotIdx++)
			{
				int particleID = m_attachedIndices[slotIdx];
				glm::vec3 slotPos = positions[particleID];
				
				// Add the attachment slot position
				m_solver->AddAttachSlot(slotPos);
				
				 // ?? NEW: Register this position as an actual fixed point for distance weights
				m_solver->RegisterFixedPoint(slotPos);
				
				// ? RESTORED: Create distance constraints for ALL particles to this fixed point
				// This allows the cloth to be influenced by the fixed point through distance constraints
				for (int i = 0; i < positions.size(); i++)
				{
					float restDistance = glm::length(slotPos - positions[i]);
					
					// Special handling for the fixed particle itself
					if (i == particleID)
					{
						// This is the actual fixed particle - distance should be 0 (completely fixed)
						m_solver->AddAttach(m_indexOffset + i, slotIdx, 0.0f);
						printf("Info(ClothObject): Fixed particle %d (global: %d) at slot %d with ZERO distance\n", 
							particleID, m_indexOffset + particleID, slotIdx);
					}
					else
					{
						// Other particles have distance constraints to this fixed point
						m_solver->AddAttach(m_indexOffset + i, slotIdx, restDistance);
					}
				}
			}
			
			printf("Info(ClothObject): Created distance constraint system with %d fixed points\n", 
				(int)m_attachedIndices.size());
			printf("Info(ClothObject): Each fixed point influences ALL particles through distance constraints\n");
			printf("Info(ClothObject): Registered %d actual fixed points for distance weight calculation\n", 
				(int)m_attachedIndices.size());
			printf("Info(ClothObject): Use 'Long Range Stretch' parameter to control constraint strength\n");
		}
	};
}