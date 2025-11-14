#include <iostream>

#include "VtEngine.hpp"
#include "GameInstance.hpp"
#include "Resource.hpp"
#include "Scene.hpp"
#include "Helper.hpp"

using namespace Velvet;

class ScenePremitiveRendering : public Scene
{
public:
	ScenePremitiveRendering() { name = "Basic / Premitive Rendering"; }

	void PopulateActors(GameInstance* game) override
	{
		Scene::SpawnCameraAndLight(game);

		//=====================================
		// 3. Objects
		//=====================================

		auto material = Resource::LoadMaterial("_Default");
		{
			material->Use();

			material->SetTexture("material.diffuse", Resource::LoadTexture("wood.png"));
			material->SetBool("material.useTexture", true);
		}

		auto cube1 = game->CreateActor("Sphere");
		{
			auto mesh = Resource::LoadMesh("sphere.obj");
			shared_ptr<MeshRenderer> renderer(new MeshRenderer(mesh, material, true));
			cube1->AddComponent(renderer);
			cube1->transform->position = glm::vec3(0.6f, 2.0f, 0.0);
			cube1->transform->scale = glm::vec3(0.5f);
		}

		auto cube2 = game->CreateActor("Cube2");
		{
			auto mesh = Resource::LoadMesh("cube.obj");
			auto renderer = make_shared<MeshRenderer>(mesh, material, true);
			cube2->AddComponent(renderer);
			cube2->transform->position = glm::vec3(2.0f, 0.5, 1.0);
		}

		auto cube3 = game->CreateActor("Cube3");
		{
			auto mesh = Resource::LoadMesh("cube.obj");
			auto renderer = make_shared<MeshRenderer>(mesh, material, true);
			cube3->AddComponent(renderer);
			cube3->Initialize(glm::vec3(-1.0f, 0.5, 2.0),
				glm::vec3(0.5f),
				glm::vec3(60, 0, 60));
		}

		SpawnInfinitePlane(game);
	}
};

class SceneColoredCubes : public Scene
{
public:
	SceneColoredCubes() { name = "Basic / Colored Cubes"; }

	void PopulateActors(GameInstance* game)  override
	{
		Scene::SpawnCameraAndLight(game);

		Scene::SpawnInfinitePlane(game);

		{
			auto whiteCube = SpawnColoredCube(game, glm::vec3(1.0, 1.0, 1.0));
			whiteCube->Initialize(glm::vec3(0, 0.25, 0),
				glm::vec3(2, 0.5f, 2));
		}

		vector<glm::vec3> colors = {
			glm::vec3(0.0f, 0.5f, 1.0f),
			glm::vec3(0.797f, 0.354f, 0.000f),
			glm::vec3(0.000f, 0.349f, 0.173f),
			glm::vec3(0.875f, 0.782f, 0.051f),
			glm::vec3(0.01f, 0.170f, 0.453f),
			glm::vec3(0.673f, 0.111f, 0.000f),
			glm::vec3(0.612f, 0.194f, 0.394f)
		};

		vector<shared_ptr<Actor>> cubes;
		static vector<glm::vec3> velocities;
		for (int i = 0; i < 50; i++)
		{
			glm::vec3 color = colors[(int)Helper::Random(0.0f, (float)colors.size())];
			auto cube = SpawnColoredCube(game, color);
			cube->Initialize(glm::vec3(Helper::Random(-3.0f, 3.0f), Helper::Random(0.3f, 0.5f), Helper::Random(-3.0f, 3.0f)), 
				glm::vec3(0.3f));
			cubes.push_back(cube);
			velocities.push_back(glm::vec3(0.0));
		}

		game->animationUpdate.Register([cubes, game]() {
			for (int i = 0; i < cubes.size(); i++)
			{
				auto cube = cubes[i];
				//cube->transform->position += Helper::RandomUnitVector() * Timer::deltaTime() * 5.0f;
				velocities[i] = Helper::Lerp(velocities[i], Helper::RandomUnitVector() * 1.0f, Timer::fixedDeltaTime());
				cube->transform->rotation += Helper::RandomUnitVector() * Timer::fixedDeltaTime() * 50.0f;
				cube->transform->position += velocities[i] * Timer::fixedDeltaTime() * 5.0f;

				if (cube->transform->position.y < 0.07f)
				{
					cube->transform->position.y = 0.07f;
				}
				if (glm::length(cube->transform->position) > 3.0f)
				{
					cube->transform->position = cube->transform->position / glm::length(cube->transform->position) * 3.0f;
				}
			}
			});
	}
};

class SceneClothSuspended : public Scene
{
public:
	SceneClothSuspended() { name = "Cloth / Suspended"; }

	void PopulateActors(GameInstance* game)  override
	{
		SpawnCameraAndLight(game);
		SpawnInfinitePlane(game);

		int clothResolution = 40;
		auto cloth = SpawnCloth(game, clothResolution);
		cloth->Initialize(glm::vec3(0.0f, 3.0f, 1.0f), glm::vec3(1.0), glm::vec3(90, 0, 0));

#ifdef SOLVER_CPU
		auto clothObj = cloth->GetComponent<VtClothObjectCPU>();
#else		
		auto clothObj = cloth->GetComponent<VtClothObjectGPU>();
#endif	
		if (clothObj)
			clothObj->SetAttachedIndices({ 0, clothResolution });

	}
};

class SceneClothAttach : public Scene
{
public:
	SceneClothAttach() { name = "Cloth / Attach"; }

	void PopulateActors(GameInstance* game)  override
	{
		SpawnCameraAndLight(game);
		SpawnInfinitePlane(game);

		auto sphere = SpawnSphere(game);
		float radius = 0.5f;
		sphere->Initialize(glm::vec3(0, radius, 0), glm::vec3(radius));

		int clothResolution = 40;
		auto cloth = SpawnCloth(game, clothResolution);
		cloth->Initialize(glm::vec3(0.0f, 1.5f, 1.0f), glm::vec3(1.0), glm::vec3(90, 0, 0));

		#ifdef SOLVER_CPU
		auto clothObj = cloth->GetComponent<VtClothObjectCPU>();
		#else		
		auto clothObj = cloth->GetComponent<VtClothObjectGPU>();
		#endif	
		if (clothObj)
			clothObj->SetAttachedIndices({ 0, clothResolution, (clothResolution + 1) * (clothResolution + 1) - 1, (clothResolution + 1) * (clothResolution) });

	}
};

class SceneClothCollision : public Scene
{
public:
	SceneClothCollision() { name = "Cloth / SDF Collision"; }

	void PopulateActors(GameInstance* game)  override
	{
		SpawnCameraAndLight(game);

		SpawnInfinitePlane(game);

		auto sphere = SpawnSphere(game);
		float radius = 0.6f;
		sphere->Initialize(glm::vec3(0, radius, -1), glm::vec3(radius));
		game->animationUpdate.Register([sphere, game, radius]() {
			float time = Timer::fixedDeltaTime() * Timer::physicsFrameCount();
			sphere->transform->position = glm::vec3(0, radius, -cos(time * 2));
			});

		int clothResolution = 16;
		auto cloth = SpawnCloth(game, clothResolution, 2);
		cloth->Initialize(glm::vec3(0, 2.5f, 0), glm::vec3(1.0));
#ifdef SOLVER_CPU
		auto clothObj = cloth->GetComponent<VtClothObjectCPU>();
#else
		auto clothObj = cloth->GetComponent<VtClothObjectGPU>();
#endif
		if (clothObj) 
			clothObj->SetAttachedIndices({ 0, clothResolution });
	}
};

class SceneClothSelfCollision : public Scene
{
public:
	SceneClothSelfCollision() { name = "Cloth / Self Collision"; }

	void PopulateActors(GameInstance* game)  override
	{
		SpawnCameraAndLight(game);
		SpawnInfinitePlane(game);

		ModifyParameter(&Global::simParams.numSubsteps, 3);
		ModifyParameter(&Global::simParams.numSubsteps, 8);
		ModifyParameter(&Global::simParams.friction, 0.3f);

		int clothResolution = 60;
		auto cloth = SpawnCloth(game, clothResolution, 1);
		cloth->Initialize(glm::vec3(0.0f, 1.5f, 1.0f), glm::vec3(1.0), glm::vec3(-15, 10, 10));

#ifdef SOLVER_CPU
		auto clothObj = cloth->GetComponent<VtClothObjectCPU>();
#else
		auto clothObj = cloth->GetComponent<VtClothObjectGPU>();
#endif
	}
};

class SceneClothFriction : public Scene
{
public:
	SceneClothFriction() { name = "Cloth / Friction"; }

	void PopulateActors(GameInstance* game)  override
	{
		SpawnCameraAndLight(game);
		SpawnInfinitePlane(game);

		ModifyParameter(&Global::simParams.friction, 0.6f);
		ModifyParameter(&Global::simParams.numSubsteps, 5);
		ModifyParameter(&Global::simParams.numIterations, 5);

		auto sphere = SpawnSphere(game);
		float radius = 0.5f;
		sphere->Initialize(glm::vec3(0, radius, 0), glm::vec3(radius));

		game->animationUpdate.Register([sphere, game, radius]() {
			float time = Timer::physicsFrameCount() * Timer::fixedDeltaTime() - 0.5f;
			if (time > 0)
			{
				sphere->transform->position = glm::vec3(sin(time), radius, 0);
			}

			if ((int)time % 4 > 1)
			{
				sphere->transform->rotation = glm::vec3(0, -time * 180, 0);
			}
			else
			{
				sphere->transform->rotation = glm::vec3(0, time * 180, 0);
			}
			});

		int clothResolution = 64;
		auto cloth = SpawnCloth(game, clothResolution, 2);
		cloth->Initialize(glm::vec3(0.0f, 1.5f, 1.0f), glm::vec3(1.0), glm::vec3(90, 0, 0));
	}
};

class SceneClothMultiple : public Scene
{
public:
	SceneClothMultiple() { name = "Cloth / Multiple Object"; }

	void PopulateActors(GameInstance* game)  override
	{
		SpawnCameraAndLight(game);
		SpawnInfinitePlane(game);

		ModifyParameter(&Global::simParams.friction, 0.6f);
		ModifyParameter(&Global::simParams.numSubsteps, 5);
		ModifyParameter(&Global::simParams.numIterations, 5);

		auto cube = SpawnColoredCube(game);
		float radius = 1.0f;
		cube->Initialize(glm::vec3(0, 0.5 * radius, 0), glm::vec3(radius));
		  
		int clothResolution = 64;

	#ifdef SOLVER_CPU
		auto solver = make_shared<VtClothSolverCPU>(clothResolution);
	#else
		auto solverActor = game->CreateActor("ClothSolver");
		auto solver = make_shared<VtClothSolverGPU>();
		solverActor->AddComponent(solver);
	#endif
		 
		{
			auto cloth = SpawnCloth(game, clothResolution, 1, solver);
			cloth->Initialize(glm::vec3(0.0f, 1.5f, 1.0f), glm::vec3(1.0), glm::vec3(90, 0, 0));
		}
		{
			auto cloth = SpawnCloth(game, clothResolution, 2, solver);
			cloth->Initialize(glm::vec3(0.0f, 1.8f, 1.0f), glm::vec3(1.0), glm::vec3(90, 0, 0));
		}
		{
			auto cloth = SpawnCloth(game, clothResolution, 3, solver);
			cloth->Initialize(glm::vec3(0.0f, 2.1f, 1.0f), glm::vec3(1.0), glm::vec3(90, 0, 0));
		}
	}
};

class SceneClothHD : public Scene
{
public:
	SceneClothHD() { name = "Cloth / High Resolution"; }

	void PopulateActors(GameInstance* game)  override
	{
		SpawnCameraAndLight(game);
		SpawnInfinitePlane(game);

		ModifyParameter(&Global::simParams.numSubsteps, 10);
		ModifyParameter(&Global::simParams.numIterations, 10);

		auto sphere = SpawnSphere(game);
		float radius = 0.6f;
		sphere->Initialize(glm::vec3(0, radius, 0), glm::vec3(radius));

		int clothResolution = 200;
		auto cloth = SpawnCloth(game, clothResolution, 2);
		cloth->Initialize(glm::vec3(0.0f, 1.5f, 1.0f), glm::vec3(1.0), glm::vec3(90, 0, 0));
	}
};

class SceneClothSwirl : public Scene
{
public:
	SceneClothSwirl() { name = "Cloth / Swirl"; }

	void PopulateActors(GameInstance* game)  override
	{
		SpawnCameraAndLight(game);
		SpawnInfinitePlane(game);

		auto sphere = SpawnSphere(game);
		float radius = 0.1f;
		sphere->GetComponent<Collider>()->enabled = false;
		sphere->Initialize(glm::vec3(0, radius, 0), glm::vec3(radius));

		int clothResolution = 36;
		auto cloth = SpawnCloth(game, clothResolution);
		cloth->Initialize(glm::vec3(0.0f, 1.5f, 1.0f), glm::vec3(1.0), glm::vec3(90, 0, 0));

#ifdef SOLVER_CPU
		auto clothObj = cloth->GetComponent<VtClothObjectCPU>();
#else
		auto clothObj = cloth->GetComponent<VtClothObjectGPU>();
#endif
		if (clothObj) 
			clothObj->SetAttachedIndices({0});

		game->animationUpdate.Register([clothObj, sphere]()  {
			float time = Timer::fixedDeltaTime() * Timer::physicsFrameCount() * 3;
			auto pos = glm::vec3(sin(time), cos(time) + 2, 0);
			#ifdef SOLVER_CPU
			clothObj->SetAttachmentPosition(0, pos);
			#else		
			clothObj->attachSlotPositions()[0] = pos;
			#endif		
			sphere->transform->position = pos;
		});
	}
};

class SceneFlag : public Scene
{
public:
	SceneFlag() { name = "Cloth / Flag"; }

	void PopulateActors(GameInstance* game) override
	{
		SpawnCameraAndLight(game);
		SpawnInfinitePlane(game);

		// Adjust simulation parameters for flag behavior
		ModifyParameter(&Global::simParams.numSubsteps, 5);
		ModifyParameter(&Global::simParams.numIterations, 8);
		ModifyParameter(&Global::simParams.damping, 0.1f);
		ModifyParameter(&Global::simParams.friction, 0.1f);
		
		// Enable wind for realistic flag movement
		ModifyParameter(&Global::simParams.enableWind, true);
		ModifyParameter(&Global::simParams.windDirection, glm::vec3(1.0f, 0.0f, 0.2f));
		ModifyParameter(&Global::simParams.windStrength, 5.0f);
		ModifyParameter(&Global::simParams.windTurbulence, 0.3f);
		ModifyParameter(&Global::simParams.windFrequency, 2.0f);

		// Create flag pole (cylinder)
		auto flagPole = SpawnFlagPole(game);
		float poleRadius = 0.05f;
		float poleHeight = 4.0f;
		
			// Ensure the pole extends from ground (y=0) to top (y=poleHeight)
		// For a cylinder mesh, the center should be at middle height, 
		// and the Y scale should make the total height equal poleHeight
		// If scale.y represents half-height (radius in Y direction), then:
		// - Position Y = poleHeight / 2 (center at middle)
		// - Scale Y = poleHeight / 2 (so total height = 2 * scale.y = poleHeight)
		//
		// But to be extra sure the pole reaches the ground, let's position it slightly lower
		float poleBottomY = -3.0f;  // Ground level
		float poleTopY = poleHeight;  // Top level
		float poleCenterY = (poleBottomY + poleTopY) / 2.0f;  // = poleHeight / 2
		float poleScaleY = (poleTopY - poleBottomY) / 2.0f;   // = poleHeight / 2
		
		flagPole->Initialize(glm::vec3(0, poleCenterY, 0), 
			glm::vec3(poleRadius, poleScaleY, poleRadius));

		// Create flag cloth
		int clothResolution = 16; // Good balance of detail and performance for flag
		auto flag = SpawnCloth(game, clothResolution, 1); // Use fabric texture 1
		
			// Override the flag material to ensure no reflectivity for realistic cloth appearance
		auto flagRenderer = flag->GetComponent<MeshRenderer>();
		if (flagRenderer) {
			MaterialProperty flagMaterialProperty;
			auto texture = Resource::LoadTexture("fabric1.jpg");
			flagMaterialProperty.preRendering = [texture](Material* mat) {
				mat->SetVec3("material.tint", glm::vec3(0.0f, 0.5f, 1.0f));
				mat->SetBool("material.useTexture", true);
				mat->SetTexture("material.diffuse", texture);
				mat->SetFloat("material.specular", 0.0f);
			};
			flagRenderer->SetMaterialProperty(flagMaterialProperty);
		}
		
		// Position flag next to the pole, oriented vertically
		// The flag should be attached to the side of the pole, not at the top
		float flagWidth = 1.5f;
		float flagHeight = 1.0f;
		
		// Position the flag so its left edge is at the pole, and it hangs down from the upper part of the pole
		// Flag center should be positioned so the top of the flag is near the top of the pole
		// But not at the very top - leave some space and make sure flag is below pole top
		float flagTopOffset = 0.8f; // Increased offset to ensure flag is well below pole top
		float flagCenterY = poleHeight - flagTopOffset - flagHeight * 0.5f;
		
		flag->transform->position = glm::vec3(poleRadius + flagWidth * 0.5f, flagCenterY, 0);
		flag->transform->scale = glm::vec3(flagWidth * 0.5f, flagHeight * 0.5f, 1.0f);
		flag->transform->rotation = glm::vec3(0, 0, 0); // No initial rotation

#ifdef SOLVER_CPU
		auto clothObj = flag->GetComponent<VtClothObjectCPU>();
#else		
		auto clothObj = flag->GetComponent<VtClothObjectGPU>();
#endif	
		if (clothObj)
		{
			// Attach left edge of flag to pole
			// IMPORTANT: Understanding the vertex storage order in GenerateClothMesh:
			// Vertices are stored row by row: for (y) { for (x) { vertices.push_back(...) } }
			// So the actual index is: y * (resolution + 1) + x
			// But VertexIndexAt function uses: x * (resolution + 1) + y (which is inconsistent!)
			
			// For a flag, we want to attach the LEFT COLUMN (x=0) to the pole
			// Left column vertices are at positions: (x=0, y=0), (x=0, y=1), (x=0, y=2), ...
			
			vector<int> attachedIndices;
			
			// Based on the actual vertex storage (row-major), left column indices are:
			for (int y = 0; y <= clothResolution; y++)
			{
				// Left column: x=0, y varies from 0 to resolution
				// Correct index for row-major storage: y * (resolution + 1) + x
				int vertexIndex = y * (clothResolution + 1) + 0;  // = y * (clothResolution + 1)
				attachedIndices.push_back(vertexIndex);
			}
			
			clothObj->SetAttachedIndices(attachedIndices);
			
			// CRUCIAL: Set the attachment slot positions to match the pole position
			// After setting attached indices, we need to update the slot positions to be at the pole
			// The GenerateAttach method will use these positions as fixed points
			
			// Calculate pole attachment positions along the left edge of the flag
			// Flag position and scale affect where the left edge is located
			glm::vec3 flagPosition = flag->transform->position;
			glm::vec3 flagScale = flag->transform->scale;
			
				// The flag's left edge should be attached to the pole surface
			float poleX = poleRadius;  // Attach to the surface of the pole, not the center
			
			// Move the attachment slot positions to the pole
			game->animationUpdate.Register([clothObj, poleX, flagPosition, flagScale, clothResolution, poleHeight, flagTopOffset = flagTopOffset]() {
				auto& slotPositions = clothObj->attachSlotPositions();
				
				// Update each attachment slot position to be along the pole
				for (int y = 0; y <= clothResolution; y++)
				{
					// Calculate the Y position along the pole for this attachment point
					float normalizedY = (float)y / (float)clothResolution; // 0 to 1 (top to bottom)
					
					// Map this to the actual flag height range
					// Flag top is at: poleHeight - flagTopOffset
					// Flag bottom is at: poleHeight - flagTopOffset - flagHeight
					float flagTop = poleHeight - flagTopOffset - 1.0f;
					float flagHeight = flagScale.y * 2.0f; // flagScale is radius, so full height is 2x
					float worldY = flagTop - normalizedY * flagHeight; // Top to bottom along flag
					
					// Set the slot position to be on the pole surface
					glm::vec3 polePosition = glm::vec3(poleX, worldY, flagPosition.z);
					
					// Make sure we don't exceed the array bounds
					if (y < slotPositions.size())
					{
						slotPositions[y] = polePosition;
					}
				}
			});
		}
	}

private:
	shared_ptr<Actor> SpawnFlagPole(GameInstance* game)
	{
		auto pole = game->CreateActor("Flag Pole");
		
		// Create material for pole (metallic look)
		auto material = Resource::LoadMaterial("_Default");
		MaterialProperty materialProperty;
		materialProperty.preRendering = [](Material* mat) {
			mat->SetVec3("material.tint", glm::vec3(0.6f, 0.6f, 0.7f)); // Metallic gray color
			mat->SetBool("material.useTexture", false);
			mat->SetFloat("material.specular", 0.8f);
		};

		// Load cylinder mesh for pole
		auto mesh = Resource::LoadMesh("cylinder.obj");
		auto renderer = make_shared<MeshRenderer>(mesh, material, true);
		renderer->SetMaterialProperty(materialProperty);
		
		// Add collider for pole (use Cube as approximation for cylinder)
		auto collider = make_shared<Collider>(ColliderType::Cube);
		
		// DISABLE the collider to prevent collision with the flag cloth
		// The flag is attached through constraints, not collision
		collider->enabled = false;
		
		pole->AddComponents({ renderer, collider });
		return pole;
	}
};

int main()
{
	//=====================================
	// 1. Create graphics
	//=====================================
	auto engine = make_shared<VtEngine>();

	//=====================================
	// 2. Instantiate actors
	//=====================================
	
	vector<shared_ptr<Scene>> scenes = {
		make_shared<SceneClothSuspended>(),
		make_shared<SceneClothAttach>(),
		make_shared<SceneClothCollision>(),
		make_shared<SceneClothSelfCollision>(),
		make_shared<SceneClothFriction>(),
		make_shared<SceneClothMultiple>(),
		make_shared<SceneClothHD>(),
		make_shared<SceneClothSwirl>(),
		make_shared<SceneFlag>(), // Add the new flag scene
		//make_shared<SceneColoredCubes>(),
		//make_shared<ScenePremitiveRendering>(),
	};
	engine->SetScenes(scenes);

	//=====================================
	// 3. Run graphics
	//=====================================
	return engine->Run();
}