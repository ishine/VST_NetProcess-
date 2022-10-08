//------------------------------------------------------------------------
// Copyright(c) 2022 natas.
//------------------------------------------------------------------------

#include "ss_controller.h"
#include "ss_cids.h"
#include "vstgui/plugin-bindings/vst3editor.h"
// �����Ҫ��ͷ�ļ�
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "AudioFile.h"
#include "params.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

using namespace Steinberg;

namespace MyCompanyName {

//------------------------------------------------------------------------
// NetProcessController Implementation
//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessController::initialize (FUnknown* context)
{
	// Here the Plug-in will be instanciated

	//---do not forget to call parent ------
	tresult result = EditControllerEx1::initialize (context);
	if (result != kResultOk)
	{
		return result;
	}

	// Here you could register some parameters
	setKnobMode(Vst::kLinearMode);
	parameters.addParameter(STR16("OSC_kEnableTwiceRepeat"), nullptr, 0, defaultEnableTwiceRepeat, Vst::ParameterInfo::kCanAutomate, kEnableTwiceRepeat);
	parameters.addParameter(STR16("OSC_kTwiceRepeatIntvalTime"), nullptr, 0, defaultTwiceRepeatIntvalTime, Vst::ParameterInfo::kCanAutomate, kTwiceRepeatIntvalTime);
	parameters.addParameter(STR16("OSC_kMaxSliceLength"), nullptr, 0, defaultMaxSliceLength, Vst::ParameterInfo::kCanAutomate, kMaxSliceLength);
	parameters.addParameter(STR16("OSC_kPitchChange"), nullptr, 0, defaultPitchChange, Vst::ParameterInfo::kCanAutomate, kPitchChange);
	//parameters.addParameter(STR16("OSC_kPrefixBufferLength"), nullptr, 0, defaultPrefixBufferLength, Vst::ParameterInfo::kCanAutomate, kPrefixBufferLength);
	parameters.addParameter(STR16("OSC_kEnabelPitchErrorCalc"), nullptr, 0, defaultEnabelPitchErrorCalc, Vst::ParameterInfo::kCanAutomate, kEnabelPitchErrorCalc);
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessController::terminate ()
{
	// Here the Plug-in will be de-instanciated, last possibility to remove some memory!

	//---do not forget to call parent ------
	return EditControllerEx1::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessController::setComponentState (IBStream* state)
{
	// Here you get the state of the component (Processor part)
	if (!state)
		return kResultFalse;

	// �����л�����ȡ����
	IBStreamer streamer(state, kLittleEndian);

	bool bVal;
	float fVal;
	if (streamer.readBool(bVal) == false) return kResultFalse;
	setParamNormalized(kEnableTwiceRepeat, bVal);
	if (streamer.readFloat(fVal) == false) return kResultFalse;
	setParamNormalized(kTwiceRepeatIntvalTime, fVal);
	if (streamer.readFloat(fVal) == false) return kResultFalse;
	setParamNormalized(kMaxSliceLength, fVal);
	if (streamer.readFloat(fVal) == false) return kResultFalse;
	setParamNormalized(kPitchChange, fVal);
	//if (streamer.readFloat(fVal) == false) return kResultFalse;
	//setParamNormalized(kPrefixBufferLength, fVal);
	if (streamer.readBool(bVal) == false) return kResultFalse;
	setParamNormalized(kEnabelPitchErrorCalc, bVal);
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessController::setState (IBStream* state)
{
	// Here you get the state of the controller

	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessController::getState (IBStream* state)
{
	// Here you are asked to deliver the state of the controller (if needed)
	// Note: the real state of your plug-in is saved in the processor

	return kResultTrue;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API NetProcessController::createView (FIDString name)
{
	// Here the Host wants to open your editor (if you have one)
	if (FIDStringsEqual (name, Vst::ViewType::kEditor))
	{
		// create your editor here and return a IPlugView ptr of it
		auto* view = new VSTGUI::VST3Editor (this, "view", "ss_editor.uidesc");
		return view;
	}
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessController::setParamNormalized (Vst::ParamID tag, Vst::ParamValue value)
{
	// called by host to update your parameters
	tresult result = EditControllerEx1::setParamNormalized (tag, value);
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessController::getParamStringByValue (Vst::ParamID tag, Vst::ParamValue valueNormalized, Vst::String128 string)
{
	// called by host to get a string for given normalized value of a specific parameter
	// (without having to set the value!)
	return EditControllerEx1::getParamStringByValue (tag, valueNormalized, string);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessController::getParamValueByString (Vst::ParamID tag, Vst::TChar* string, Vst::ParamValue& valueNormalized)
{
	// called by host to get a normalized value from a string representation of a specific parameter
	// (without having to set the value!)
	return EditControllerEx1::getParamValueByString (tag, string, valueNormalized);
}

//------------------------------------------------------------------------
} // namespace MyCompanyName
