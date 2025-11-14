#include "GUI.hpp"

#include "Scene.hpp"
#include "VtEngine.hpp"
#include "Timer.hpp"

using namespace Velvet;

#define SHORTCUT_BOOL(key, variable) if (Global::input->GetKeyDown(key)) variable = !variable

inline GUI* g_Gui;
const float k_leftWindowWidth = 380.0f;  // 增加宽度从250到380
const float k_rightWindowWidth = 330.0f;

void HelpMarker(const char* desc)
{
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

// Implementation of VtSimParams::OnGUI()
void VtSimParams::OnGUI()
{
	// Update physics frame rate from Timer if different
	int currentTimerFrameRate = Timer::GetPhysicsFrameRate();
	if (physicsFrameRate != currentTimerFrameRate)
	{
		physicsFrameRate = currentTimerFrameRate;
	}

	// Physics Update Frequency control
	if (IMGUI_LEFT_LABEL(ImGui::SliderInt, "Physics FPS", &physicsFrameRate, 10, 300))
	{
		Timer::SetPhysicsFrameRate(physicsFrameRate);
	}
	HelpMarker("Controls the physics simulation update frequency");
	
	ImGui::Separator();
	
	IMGUI_LEFT_LABEL(ImGui::SliderInt, "Num Substeps", &numSubsteps, 1, 20);
	IMGUI_LEFT_LABEL(ImGui::SliderInt, "Num Iterations", &numIterations, 1, 20);
	IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Max Speed", &maxSpeed, 1e-2f, 100);
	
	ImGui::Separator();
	
	// Forces Section
	ImGui::Text("Forces:");
	IMGUI_LEFT_LABEL(ImGui::DragFloat3, "Gravity", (float*)&gravity, 0.1f, -50, 50);
	
	// Wind Controls
	IMGUI_LEFT_LABEL(ImGui::Checkbox, "Enable Wind", &enableWind);
	HelpMarker("Enable wind forces affecting the cloth");
	
	if (enableWind)
	{
		ImGui::Indent(15);
		
		IMGUI_LEFT_LABEL(ImGui::DragFloat3, "Wind Direction", (float*)&windDirection, 0.1f, -10, 10);
		HelpMarker("Wind direction vector (will be normalized automatically)");
		
		IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Wind Strength", &windStrength, 0.0f, 20.0f, "%.1f");
		HelpMarker("Base wind strength multiplier");
		
		IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Turbulence", &windTurbulence, 0.0f, 1.0f, "%.2f");
		HelpMarker("Random wind variation (0 = steady, 1 = very turbulent)");
		
		IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Frequency", &windFrequency, 0.1f, 10.0f, "%.1f");
		HelpMarker("Speed of wind turbulence changes");
		
		// Wind Presets
		ImGui::Text("Presets:");
		if (ImGui::Button("Gentle Breeze"))
		{
			windDirection = glm::vec3(1, 0, 0);
			windStrength = 3.0f;
			windTurbulence = 0.2f;
			windFrequency = 1.0f;
		}
		ImGui::SameLine();
		if (ImGui::Button("Strong Wind"))
		{
			windDirection = glm::vec3(1, 0.2f, 0);
			windStrength = 8.0f;
			windTurbulence = 0.4f;
			windFrequency = 3.0f;
		}
		ImGui::SameLine();
		if (ImGui::Button("Hurricane"))
		{
			windDirection = glm::vec3(1, -0.3f, 0.5f);
			windStrength = 15.0f;
			windTurbulence = 0.8f;
			windFrequency = 6.0f;
		}
		
		if (ImGui::Button("Updraft"))
		{
			windDirection = glm::vec3(0.2f, 1, 0);
			windStrength = 6.0f;
			windTurbulence = 0.3f;
			windFrequency = 2.0f;
		}
		ImGui::SameLine();
		if (ImGui::Button("Swirling"))
		{
			windDirection = glm::vec3(0.7f, 0, 0.7f);
			windStrength = 10.0f;
			windTurbulence = 0.6f;
			windFrequency = 4.0f;
		}
		
		ImGui::Indent(-15);
	}
	
	IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Damping", &damping, 0, 10.0f);
	IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Friction", &friction, 0, 1);
	IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Collision Margin", &collisionMargin, 0, 0.5);
	IMGUI_LEFT_LABEL(ImGui::Checkbox, "Enable Self Collision", &enableSelfCollision);
	IMGUI_LEFT_LABEL(ImGui::SliderInt, "Interleaved Hash", &interleavedHash, 1, 10);
	ImGui::Separator();
	IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Relaxation Factor", &relaxationFactor, 0, 3.0);
	//IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Bend Compliance", &bendCompliance, 1e-3, 100.0, "%.3f", ImGuiSliderFlags_Logarithmic);
	IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Long Range Stretch", &longRangeStretchiness, 1.0, 2.0, "%.3f");
	
	ImGui::Separator();
	
	// Distance-based Weight System
	IMGUI_LEFT_LABEL(ImGui::Checkbox, "Use Distance Weights", &useDistanceBasedWeights);
	HelpMarker("Use distance to fixed points for weight calculation instead of mass-based weights");
	
	if (useDistanceBasedWeights)
	{
		ImGui::Indent(15);
		
		IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Weight Falloff", &distanceWeightFalloff, 0.5f, 5.0f, "%.1f");
		HelpMarker("Falloff exponent for distance-based weights (higher = more influence for closer points)");
		
		IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Max Distance", &maxDistanceInfluence, 1.0f, 50.0f, "%.1f");
		HelpMarker("Maximum distance considered for weight calculation (world units)");
		
		ImGui::Text("How it works:");
		ImGui::TextWrapped("- Vertices closer to fixed points move less");
		ImGui::TextWrapped("- Vertices farther from fixed points move more");
		ImGui::TextWrapped("- Higher falloff = stronger distance influence");
		
		ImGui::Indent(-15);
	}
	
	ImGui::Separator();
	
	// Convergence Detection Section
	IMGUI_LEFT_LABEL(ImGui::Checkbox, "Enable Convergence Check", &enableConvergenceCheck);
	HelpMarker("Enable convergence detection to monitor simulation stability");
	
	if (enableConvergenceCheck)
	{
		ImGui::Indent(15);
		
		// Threshold Controls
		ImGui::Text("Convergence Thresholds:");
		IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Constraint Violation", &convergenceThreshold, 0.001f, 0.1f, "%.4f");
		HelpMarker("Maximum acceptable constraint violation ratio (0-1)");
		
		IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Position Change", &positionChangeThreshold, 1e-5f, 1e-2f, "%.6f", ImGuiSliderFlags_Logarithmic);
		HelpMarker("Maximum acceptable position change magnitude per frame");
		
		IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Velocity Limit Ratio", &velocityLimitRatio, 0.01f, 0.5f, "%.3f");
		HelpMarker("Maximum acceptable ratio of particles hitting velocity limit");
		
		ImGui::Separator();
		
		// Current Metrics Display
		ImGui::Text("Current Metrics:");
		
		// Constraint Violation
		ImVec4 constraintColor = (avgConstraintViolation <= convergenceThreshold) ? 
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, constraintColor);
		ImGui::Text("Constraint Violation: %.6f", avgConstraintViolation);
		ImGui::PopStyleColor();
		
		// Position Change
		ImVec4 positionColor = (avgPositionChange <= positionChangeThreshold) ? 
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, positionColor);
		ImGui::Text("Position Change: %.6f", avgPositionChange);
		ImGui::PopStyleColor();
		
		// Velocity Limit Ratio
		ImVec4 velocityColor = (velocityLimitTriggerRatio <= velocityLimitRatio) ? 
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, velocityColor);
		ImGui::Text("Velocity Limit Ratio: %.3f%%", velocityLimitTriggerRatio * 100.0f);
		ImGui::PopStyleColor();
		
		ImGui::Separator();
		
		// Convergence Status
		ImVec4 statusColor = isConverged ? 
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
		if (isConverged)
		{
			ImGui::Text("Status: CONVERGED (%d frames)", convergenceFrameCount);
		}
		else
		{
			ImGui::Text("Status: NOT CONVERGED");
		}
		ImGui::PopStyleColor();
		
		ImGui::Indent(-15);
	}

	ImGui::Separator();
	
	// Iteration Stability Detection Section
	IMGUI_LEFT_LABEL(ImGui::Checkbox, "Enable Iteration Stability", &enableIterationStability);
	HelpMarker("Monitor position changes between solver iterations to detect instability");
	
	if (enableIterationStability)
	{
		ImGui::Indent(15);
		
		// Iteration Stability Controls
		ImGui::Text("Iteration Stability Settings:");
		IMGUI_LEFT_LABEL(ImGui::SliderFloat, "Change Threshold", &iterationChangeThreshold, 1e-6f, 1e-2f, "%.6f", ImGuiSliderFlags_Logarithmic);
		HelpMarker("Maximum allowed position change between consecutive iterations");
		
		IMGUI_LEFT_LABEL(ImGui::SliderInt, "Required Stable Iterations", &requiredStableIterations, 1, 10);
		HelpMarker("Number of consecutive stable iterations required for stability");
		
		IMGUI_LEFT_LABEL(ImGui::Checkbox, "Enable Early Exit", &enableEarlyExit);
		HelpMarker("Exit iteration loop early when stability is achieved to improve performance");
		
		ImGui::Separator();
		
		// Iteration Stability Metrics Display
		ImGui::Text("Iteration Stability Metrics:");
		
		// Average iteration change
		ImVec4 iterChangeColor = (avgIterationChange <= iterationChangeThreshold) ? 
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, iterChangeColor);
		ImGui::Text("Avg Iteration Change: %.6f", avgIterationChange);
		ImGui::PopStyleColor();
		
		// Maximum iteration change
		ImVec4 maxChangeColor = (maxIterationChange <= iterationChangeThreshold) ? 
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, maxChangeColor);
		ImGui::Text("Max Iteration Change: %.6f", maxIterationChange);
		ImGui::PopStyleColor();
		
		// Stability status
		ImVec4 stabilityColor = isIterationStable ? 
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, stabilityColor);
		if (isIterationStable)
		{
			ImGui::Text("Iteration Status: STABLE (%d/%d)", stableIterationCount, requiredStableIterations);
		}
		else
		{
			ImGui::Text("Iteration Status: UNSTABLE");
		}
		ImGui::PopStyleColor();
		
		// Performance info
		ImGui::Text("Iterations Used: %d/%d", actualIterationsUsed, numIterations);
		if (enableEarlyExit && actualIterationsUsed < numIterations)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "(Early Exit)");
		}
		
		ImGui::Indent(-15);
	}
}

struct SolverTiming
{
	int count = 0;

	vector<string> labels = {
		"SetParams",
		//"Initialize",
		"Predict",
		"SolveStretch",
		"SolveAttach",
		//"SolveBending",
		"ApplyDeltas",
		"CollideSDFs",
		"CollideParticles",
		"Finalize",
		"UpdateNormals",

		"HashParticle",
		"HashSort",
		"HashBuildCell",
		"HashCache",

		"Total",
	};

	unordered_map<string, double> label2time;
	unordered_map<string, double> label2avgTime;

	ImVec4 color_high = ImVec4(1.000f, 0.244f, 0.000f, 1.000f);
	ImVec4 color_mid = ImVec4(1.000f, 0.602f, 0.000f, 1.000f);
	ImVec4 color_low = ImVec4(1.000f, 0.889f, 0.000f, 1.000f);
	ImVec4 color_disabled = ImVec4(0.5f, 0.5f, 0.5f, 1.000f);
	
	void DisplayKernelTiming(const string name, bool autoColor = true)
	{
		double total = label2avgTime["Total"];
		bool shouldPop = false;
		float percentage = (total > 0) ? float(label2avgTime[name] / total * 100) : 0.0f;
		if (autoColor )
		{
			if (percentage > 10 || percentage == 0.0f)
			{
				ImVec4 textColor = color_low;
				if (percentage > 30)
					textColor = color_high;
				else if (percentage > 10)
					textColor = color_mid;
				else if (percentage == 0.0f)
					textColor = color_disabled;
				ImGui::PushStyleColor(ImGuiCol_Text, textColor);
				shouldPop = true;
			}
		}

		ImGui::TableNextColumn(); 
		ImGui::Text(name.c_str()); 
		ImGui::TableNextColumn(); 
		ImGui::Text("%.2f ms", label2time[name]); 
		ImGui::TableNextColumn(); 
		ImGui::Text("%.2f ms", label2avgTime[name] / count); 
		ImGui::TableNextColumn(); 
		ImGui::Text("%.2f %%", percentage);

		if (shouldPop)
		{
			ImGui::PopStyleColor();
		}
	}

	void Update()
	{
		if (Timer::PeriodicUpdate("GUI_SOLVER", 0.2f))
		{
			if (Timer::frameCount() < 2)
			{
				count = 0;
				for (const auto& label : labels)
				{
					label2avgTime[label] = 0;
				}
				label2avgTime["KernelSum"] = 0;
			}

			label2time["KernelSum"] = 0;
			for (const auto& label : labels)
			{
				label2time[label] = Timer::GetTimerGPU("Solver_" + label);
				label2avgTime[label] += label2time[label];

				if (label != "Total" && label != "Initialize") label2time["KernelSum"] += label2time[label];
			}

			label2avgTime["KernelSum"] += label2time["KernelSum"];
			count++;
		}
	}

	void OnGUI()
	{
		if (!ImGui::CollapsingHeader("Solver timing"))// , ImGuiTreeNodeFlags_DefaultOpen))
		{
			Global::gameState.detailTimer = false;
			return;
		}
		Global::gameState.detailTimer = true;

		//float averageGPUTime = (float)(label2avgTime["KernelSum"] / count);
		//int averageFPS = (averageGPUTime > 0.0f) ? (int)(1000.0f / (averageGPUTime)) : 0;
		//ImGui::Text("Avg Kernel Time: %.2f ms (%d fps)", averageGPUTime, averageFPS);
		HelpMarker("solver_total = kernel_sum + cuda_dispatch_time");

		static bool hasPrinted = false;
		int printAtFrame = 300;
		if (!hasPrinted && Timer::physicsFrameCount() == printAtFrame)
		{
			hasPrinted = true;
			fmt::print("Info(GUI): Average solver time at frame({}) is: {:.3f} ms\n", printAtFrame, label2avgTime["KernelSum"] / count);
		}

		if (ImGui::BeginTable("timing", 4))//,  ImGuiTableFlags_BordersOuter))
		{
			//ImGui::PushItemWidth(20);            
			ImGui::TableSetupColumn("Kernel");
			ImGui::TableSetupColumn("Time (ms)");
			ImGui::TableSetupColumn("Avg (ms)");
			ImGui::TableSetupColumn("%");
			ImGui::TableHeadersRow();

			for (int i = 0; i < labels.size() -1; i++)
			{
				auto& label = labels[i];
				DisplayKernelTiming(label);
			}
			DisplayKernelTiming(string("KernelSum"), false);
			DisplayKernelTiming(labels[labels.size() - 1], false);

			ImGui::EndTable();
		}
	}
};

struct PerformanceStat
{
	float deltaTime = 0;
	int frameRate = 0;
	int frameCount = 0;
	int physicsFrameCount = 0;

	float graphValues[180] = {};
	int graphIndex = 0;
	float graphAverage = 0.0f;

	double cpuTime = 0;
	// include solver_time + cuda_sync_time
	double gpuTime = 0;
	double solverTimeGPU = 0;
	double solverTimeCPU = 0;

	void Update()
	{
		if (Global::gameState.pause) return;

		const auto& game = Global::game;
		float elapsedTime = Timer::elapsedTime();
		float deltaTimeMiliseconds = Timer::deltaTime() * 1000;

		frameCount = Timer::frameCount();
		physicsFrameCount = Timer::physicsFrameCount();

		if (Timer::PeriodicUpdate("GUI_FAST", Timer::fixedDeltaTime()))
		{
			#ifdef SOLVER_CPU
			graphValues[graphIndex] = (float)Timer::GetTimer("Solver_Total")*1000;
			#else	
			graphValues[graphIndex] = (float)Timer::GetTimerGPU("Solver_Total");
			#endif	
			graphIndex = (graphIndex + 1) % IM_ARRAYSIZE(graphValues);
		}

		if (Timer::PeriodicUpdate("GUI_SLOW", 0.3f))
		{
			deltaTime = deltaTimeMiliseconds;
			frameRate = elapsedTime > 0 ? (int)(frameCount / elapsedTime) : 0;
			cpuTime = Timer::GetTimer("CPU_TIME") * 1000;
			gpuTime = Timer::GetTimer("GPU_TIME") * 1000;
			solverTimeGPU = Timer::GetTimerGPU("Solver_Total");
			solverTimeCPU = Timer::GetTimer("Solver_Total") * 1000;

			for (int n = 0; n < IM_ARRAYSIZE(graphValues); n++)
				graphAverage += graphValues[n];
			graphAverage /= (float)IM_ARRAYSIZE(graphValues);
		}
	}

	void OnGUI()
	{
		if (ImGui::BeginTable("stat", 2, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableNextColumn(); ImGui::Text("Render Frame: ");
			ImGui::TableNextColumn(); ImGui::Text("%d", frameCount);
			ImGui::TableNextColumn(); ImGui::Text("Physics Frame: ");
			ImGui::TableNextColumn(); ImGui::Text("%d", physicsFrameCount);
			ImGui::TableNextColumn(); ImGui::Text("Render FrameRate: ");
			ImGui::TableNextColumn(); ImGui::Text("%d FPS", frameRate);
			ImGui::TableNextColumn(); ImGui::Text("Physics FrameRate: ");
			ImGui::TableNextColumn(); ImGui::Text("%d FPS", Timer::GetPhysicsFrameRate());
			ImGui::TableNextColumn(); ImGui::Text("CPU time: ");
			ImGui::TableNextColumn(); ImGui::Text("%.2f ms", cpuTime);
			ImGui::TableNextColumn(); ImGui::Text("GPU time: "); 
			ImGui::TableNextColumn(); ImGui::Text("%.2f ms", gpuTime); HelpMarker("gpu_time = solver_time + cuda_synchronize_time");
			ImGui::TableNextColumn(); ImGui::Text("Num Particles: ");
			ImGui::TableNextColumn(); ImGui::Text("%d", Global::simParams.numParticles);
			ImGui::EndTable();
		}

		ImGui::Dummy(ImVec2(0, 5));
		ImGui::PushItemWidth(-FLT_MIN);
		#ifdef SOLVER_CPU
		auto overlay = fmt::format("Solver: {:.2f} ms ({:.2f} FPS)", solverTimeCPU, solverTimeCPU > 0 ? (1000.0 / solverTimeCPU) : 0);
		#else	
		auto overlay = fmt::format("Solver: {:.2f} ms ({:.2f} FPS)", solverTimeGPU, solverTimeGPU > 0 ? (1000.0 / solverTimeGPU) : 0);
		#endif	
		ImGui::PlotLines("##", graphValues, IM_ARRAYSIZE(graphValues), graphIndex, overlay.c_str(),
			0, graphAverage * 2.0f, ImVec2(0, 80.0f));
		ImGui::Dummy(ImVec2(0, 5));

	}
};

struct ConstraintViolationPlot
{
	static const int PLOT_SIZE = 180;
	
	float violationValues[PLOT_SIZE] = {};
	float positionChangeValues[PLOT_SIZE] = {};
	float velocityLimitValues[PLOT_SIZE] = {};
	int plotIndex = 0;
	
	float maxViolation = 0.01f;      // Adaptive scale for violation
	float maxPositionChange = 0.001f; // Adaptive scale for position change
	float maxVelocityLimit = 1.0f;   // Fixed scale for velocity limit ratio
	
	void Update()
	{
		if (Global::gameState.pause) return;
		
		// Update only when convergence detection is enabled
		if (!Global::simParams.enableConvergenceCheck) return;
		
		// Update every physics frame to get smooth curves
		if (Timer::PeriodicUpdate("GUI_CONVERGENCE", Timer::fixedDeltaTime()))
		{
			// Get current values from simulation parameters
			float currentViolation = Global::simParams.avgConstraintViolation;
			float currentPosChange = Global::simParams.avgPositionChange;
			float currentVelLimit = Global::simParams.velocityLimitTriggerRatio;
			
			// Store values in circular buffer
			violationValues[plotIndex] = currentViolation;
			positionChangeValues[plotIndex] = currentPosChange;
			velocityLimitValues[plotIndex] = currentVelLimit;
			
			// Update adaptive scales
			maxViolation = max(maxViolation, currentViolation * 1.2f);
			maxPositionChange = max(maxPositionChange, currentPosChange * 1.2f);
			
			// Advance circular buffer index
			plotIndex = (plotIndex + 1) % PLOT_SIZE;
		}
	}
	
	void OnGUI()
	{
		if (!Global::simParams.enableConvergenceCheck) return;
		
		if (!ImGui::CollapsingHeader("Convergence Metrics")) return;
		
		ImGui::PushItemWidth(-FLT_MIN);
		
		// Constraint Violation Plot
		{
			float currentViolation = Global::simParams.avgConstraintViolation;
			float threshold = Global::simParams.convergenceThreshold;
			
			auto overlay = fmt::format("Constraint Violation: {:.6f} (Threshold: {:.6f})", 
				currentViolation, threshold);
			
			ImVec4 plotColor = (currentViolation <= threshold) ? 
				ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
			
			ImGui::PushStyleColor(ImGuiCol_PlotLines, plotColor);
			ImGui::PlotLines("##ConstraintViolation", violationValues, PLOT_SIZE, plotIndex, 
				overlay.c_str(), 0, maxViolation, ImVec2(0, 60.0f));
			ImGui::PopStyleColor();
			
			// Draw threshold line
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 plotPos = ImGui::GetItemRectMin();
			ImVec2 plotSize = ImGui::GetItemRectSize();
			
			if (maxViolation > 0)
			{
				float thresholdY = plotPos.y + plotSize.y * (1.0f - threshold / maxViolation);
				if (thresholdY >= plotPos.y && thresholdY <= plotPos.y + plotSize.y)
				{
					drawList->AddLine(
						ImVec2(plotPos.x, thresholdY),
						ImVec2(plotPos.x + plotSize.x, thresholdY),
						IM_COL32(255, 255, 0, 128), 1.0f
					);
				}
			}
		}
		
		ImGui::Dummy(ImVec2(0, 5));
		
		// Position Change Plot
		{
			float currentPosChange = Global::simParams.avgPositionChange;
			float threshold = Global::simParams.positionChangeThreshold;
			
			auto overlay = fmt::format("Position Change: {:.6f} (Threshold: {:.6f})", 
				currentPosChange, threshold);
			
			ImVec4 plotColor = (currentPosChange <= threshold) ? 
				ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
			
			ImGui::PushStyleColor(ImGuiCol_PlotLines, plotColor);
			ImGui::PlotLines("##PositionChange", positionChangeValues, PLOT_SIZE, plotIndex, 
				overlay.c_str(), 0, maxPositionChange, ImVec2(0, 60.0f));
			ImGui::PopStyleColor();
			
			// Draw threshold line
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 plotPos = ImGui::GetItemRectMin();
			ImVec2 plotSize = ImGui::GetItemRectSize();
			
			if (maxPositionChange > 0)
			{
				float thresholdY = plotPos.y + plotSize.y * (1.0f - threshold / maxPositionChange);
				if (thresholdY >= plotPos.y && thresholdY <= plotPos.y + plotSize.y)
				{
					drawList->AddLine(
						ImVec2(plotPos.x, thresholdY),
						ImVec2(plotPos.x + plotSize.x, thresholdY),
						IM_COL32(255, 255, 0, 128), 1.0f
					);
				}
			}
		}
		
		ImGui::Dummy(ImVec2(0, 5));
		
		// Velocity Limit Ratio Plot
		{
			float currentVelLimit = Global::simParams.velocityLimitTriggerRatio;
			float threshold = Global::simParams.velocityLimitRatio;
			
			auto overlay = fmt::format("Velocity Limit Ratio: {:.3f}% (Threshold: {:.3f}%)", 
				currentVelLimit * 100.0f, threshold * 100.0f);
			
			ImVec4 plotColor = (currentVelLimit <= threshold) ? 
				ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.8f, 0.4f, 1.0f, 1.0f);
			
			ImGui::PushStyleColor(ImGuiCol_PlotLines, plotColor);
			ImGui::PlotLines("##VelocityLimit", velocityLimitValues, PLOT_SIZE, plotIndex, 
				overlay.c_str(), 0, maxVelocityLimit, ImVec2(0, 60.0f));
			ImGui::PopStyleColor();
			
			// Draw threshold line
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 plotPos = ImGui::GetItemRectMin();
			ImVec2 plotSize = ImGui::GetItemRectSize();
			
			float thresholdY = plotPos.y + plotSize.y * (1.0f - threshold / maxVelocityLimit);
			if (thresholdY >= plotPos.y && thresholdY <= plotPos.y + plotSize.y)
			{
				drawList->AddLine(
					ImVec2(plotPos.x, thresholdY),
					ImVec2(plotPos.x + plotSize.x, thresholdY),
					IM_COL32(255, 255, 0, 128), 1.0f
				);
			}
		}
		
		ImGui::PopItemWidth();
		
		// Overall convergence status
		ImGui::Dummy(ImVec2(0, 5));
		ImVec4 statusColor = Global::simParams.isConverged ? 
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
		if (Global::simParams.isConverged)
		{
			ImGui::Text("Overall Status: CONVERGED (%d frames)", Global::simParams.convergenceFrameCount);
		}
		else
		{
			ImGui::Text("Overall Status: NOT CONVERGED");
		}
		ImGui::PopStyleColor();
	}
};

void GUI::RegisterDebug(function<void()> callback)
{
	g_Gui->m_showDebugInfo.Register(callback);
}

void GUI::RegisterDebugOnce(function<void()> callback)
{
	g_Gui->m_showDebugInfoOnce.Register(callback);
}

void GUI::RegisterDebugOnce(const string& debugMessage)
{
	//vprintf(debugMessage, args);
	g_Gui->m_showDebugInfoOnce.Register([debugMessage]() {
		ImGui::Text(debugMessage.c_str());
		});
}

GUI::GUI(GLFWwindow* window)
{
	g_Gui = this;
	m_window = window;

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.IniFilename = NULL;
	io.Fonts->AddFontFromFileTTF("Assets/DroidSans.ttf", 19);
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	CustomizeStyle();

	// Setup Platform/Renderer backends
	const char* glsl_version = "#version 330";
	ImGui_ImplGlfw_InitForOpenGL(m_window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	m_deviceName = string((char*)glGetString(GL_RENDERER));
	m_deviceName = m_deviceName.substr(0, m_deviceName.find("/"));
}

void GUI::OnUpdate()
{
	//static bool show_demo_window = true;
	//ImGui::ShowDemoWindow(&show_demo_window);
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	glfwGetWindowSize(m_window, &m_canvasWidth, &m_canvasHeight);

	ShowSceneWindow();
	ShowOptionWindow();
	ShowStatWindow();
}

void GUI::Render()
{
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GUI::ClearCallback()
{
	m_showDebugInfo.Clear();
	m_showDebugInfoOnce.Clear();
}

void GUI::ShutDown()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void GUI::CustomizeStyle()
{
	ImGui::StyleColorsDark();

	auto style = &ImGui::GetStyle();
	style->SelectableTextAlign = ImVec2(0, 0.5);
	style->WindowPadding = ImVec2(10, 12);
	style->WindowRounding = 6;
	style->GrabRounding = 8;
	style->FrameRounding = 6;
	style->WindowTitleAlign = ImVec2(0.5, 0.5);

	style->Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.7f);
	style->Colors[ImGuiCol_TitleBg] = style->Colors[ImGuiCol_WindowBg];
	style->Colors[ImGuiCol_TitleBgActive] = style->Colors[ImGuiCol_TitleBg];
	style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.325f, 0.325f, 0.325f, 1.0f);
	style->Colors[ImGuiCol_FrameBg] = ImVec4(0.114f, 0.114f, 0.114f, 1.0f);
	style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
	style->Colors[ImGuiCol_Button] = ImVec4(0.46f, 0.46f, 0.46f, 0.46f);
	style->Colors[ImGuiCol_CheckMark] = ImVec4(0.851f, 0.851f, 0.851f, 1.0f);

	style->Colors[ImGuiCol_TableBorderLight] = ImVec4(1.0f, 1.0f, 1.0f, 0.3f);
	style->Colors[ImGuiCol_TableBorderStrong] = ImVec4(1.0f, 1.0f, 1.0f, 0.6f);
	//ImGui::StyleColorsClassic();ImGuiCol_TableBorderLight
}

void GUI::ShowSceneWindow()
{
	ImGui::SetNextWindowSize(ImVec2(k_leftWindowWidth, (m_canvasHeight - 60.0f) * 0.4f));
	ImGui::SetNextWindowPos(ImVec2(20, 20));
	ImGui::Begin("Scene", NULL, k_windowFlags);

	const auto& scenes = Global::engine->scenes;

	for (unsigned int i = 0; i < scenes.size(); i++)
	{
		auto scene = scenes[i];
		auto label = scene->name;
		if (ImGui::Selectable(label.c_str(), Global::engine->sceneIndex == i, 0, ImVec2(0, 28)))
		{
			Global::engine->SwitchScene(i);
		}
	}

	ImGui::End();
}

void GUI::ShowOptionWindow()
{
	ImGui::SetNextWindowSize(ImVec2(k_leftWindowWidth, (m_canvasHeight - 60.0f) * 0.6f));
	ImGui::SetNextWindowPos(ImVec2(20, 40 + (m_canvasHeight - 60.0f) * 0.4f));
	ImGui::Begin("Options", NULL, k_windowFlags);

	ImGui::PushItemWidth(-FLT_MIN);

	if (ImGui::Button("Reset (R)", ImVec2(-FLT_MIN, 0)))
	{
		Global::engine->Reset();
	}
	ImGui::Dummy(ImVec2(0.0f, 10.0f));

	{
		static bool radio = false;
		ImGui::Checkbox("Pause (P, O)", &Global::gameState.pause);
		Global::input->ToggleOnKeyDown(GLFW_KEY_P, Global::gameState.pause);
		ImGui::Checkbox("Draw Particles (K)", &Global::gameState.drawParticles);
		Global::input->ToggleOnKeyDown(GLFW_KEY_K, Global::gameState.drawParticles);
		ImGui::Checkbox("Draw Wireframe (L)", &Global::gameState.renderWireframe);
		Global::input->ToggleOnKeyDown(GLFW_KEY_L, Global::gameState.renderWireframe);
		ImGui::Dummy(ImVec2(0.0f, 10.0f));
	}

	if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		Global::simParams.OnGUI();
	}

	ImGui::End();
}

void GUI::ShowStatWindow()
{
	ImGui::SetNextWindowSize(ImVec2(k_rightWindowWidth * 1.1f, 0));
	ImGui::SetNextWindowPos(ImVec2(m_canvasWidth - k_rightWindowWidth * 1.1f - 20, 20.0f));
	ImGui::Begin("Statistics", NULL, k_windowFlags);
	#ifdef SOLVER_CPU
	ImGui::Text("Cloth Solver: CPU");
	#else
	ImGui::Text("Cloth Solver: GPU");
	#endif
	ImGui::Text("Device:  %s", m_deviceName.c_str());

	static PerformanceStat stat;
	stat.Update();
	stat.OnGUI();

	static SolverTiming solverTiming;
	solverTiming.Update();
	solverTiming.OnGUI();

	// Add Convergence Metrics Plot
	static ConstraintViolationPlot convergencePlot;
	convergencePlot.Update();
	convergencePlot.OnGUI();

	if (!m_showDebugInfo.empty() || !m_showDebugInfoOnce.empty())
	{
		if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
		{
			m_showDebugInfo.Invoke();
			m_showDebugInfoOnce.Invoke();

			if (!Global::gameState.pause) m_showDebugInfoOnce.Clear();
		}
	}

	ImGui::End();
}