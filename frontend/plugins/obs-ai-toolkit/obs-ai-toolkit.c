/*
 * OBS AI Toolkit
 * Native AI-powered features for OBS Studio
 *
 * Features:
 * - AI Health Dashboard: Real-time stream health monitoring
 * - Quality Advisor: Intelligent recommendations during streaming
 * - Video Analyzer: Combined transcription and highlight detection with video player
 */

#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-ai-toolkit", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "AI-powered toolkit for streamers and content creators";
}

/* Forward declarations */
void InitAIHealthDock(void);
void FreeAIHealthDock(void);

void InitVideoAnalyzer(void);
void FreeVideoAnalyzer(void);

bool obs_module_load(void)
{
	InitAIHealthDock();
	InitVideoAnalyzer();
	return true;
}

void obs_module_unload(void)
{
	FreeAIHealthDock();
	FreeVideoAnalyzer();
}

