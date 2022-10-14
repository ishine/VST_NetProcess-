//------------------------------------------------------------------------
// Copyright(c) 2022 natas.
//------------------------------------------------------------------------

#include "ss_processor.h"
#include "ss_cids.h"

#include "base/source/fstreamer.h"
// �����Ҫ��ͷ�ļ�
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "AudioFile.h"
#include "httplib.h"
#include <numeric>
#include <Query.h>
#include "params.h"
#include "json/json.h"
#include <windows.h>
#include <filesystem>

using namespace Steinberg;

namespace MyCompanyName {
//------------------------------------------------------------------------
// NetProcessProcessor
//------------------------------------------------------------------------

	// Resample
	void func_audio_resample(FUNC_SRC_SIMPLE dllFuncSrcSimple, float* fInBuffer, float* fOutBuffer, double src_ratio, long lInSize, long lOutSize) {
		SRC_DATA data;
		data.src_ratio = src_ratio;
		data.input_frames = lInSize;
		data.output_frames = lOutSize;
		data.data_in = fInBuffer;
		data.data_out = fOutBuffer;
		int error = dllFuncSrcSimple(&data, SRC_SINC_FASTEST, 1);

		/*if (error > 0) {
			char buff[100];
			const char* cError = src_strerror(error);
			snprintf(buff, sizeof(buff), "Resample error%s\n", cError);
			OutputDebugStringA(buff);
		}*/
	}

	// ���ڼ���һ����д���������Ч���ݴ�С
	long func_cacl_read_write_buffer_data_size(long lBufferSize, long lReadPos, long lWritePos) {
		long inputBufferSize;
		if (lReadPos < lWritePos) {
			inputBufferSize = lWritePos - lReadPos;
		}
		else {
			inputBufferSize = lWritePos + lBufferSize - lReadPos;
		}
		return inputBufferSize;
	}

// ��������������Ϊ��ʱ���ڵ������߳�����У��������߳̿��ٱ���
	void func_do_voice_transfer(
		int iNumberOfChanel,									// ͨ������
		double dProjectSampleRate,								// ��Ŀ������
		//std::queue<double>* qModelInputSampleQueue,				// ģ����ζ���
		//std::queue<double>* qModelOutputSampleQueue,			// ģ�ͷ��ض���
		
		long lModelInputOutputBufferSize,

		float* fModeulInputSampleBuffer,
		long* lModelInputSampleBufferReadPos,
		long* lModelInputSampleBufferWritePos,

		float* fModeulOutputSampleBuffer,
		long* lModelOutputSampleBufferReadPos,
		long* lModelOutputSampleBufferWritePos,

		float fPrefixLength,
		float fDropSuffixLength,
		bool bRepeat,
		float fRepeatTime,
		float fPitchChange,
		bool bCalcPitchError,
		roleStruct roleStruct,
		FUNC_SRC_SIMPLE dllFuncSrcSimple
) {
	// ������Ƶ���ݵ��ļ�
	// ��ȡ��ǰдָ���λ��
	long lTmpModelInputSampleBufferWritePos = *lModelInputSampleBufferWritePos;
	// ������Ի�ȡ�����ݳ���
	long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, *lModelInputSampleBufferReadPos, lTmpModelInputSampleBufferWritePos);
		
	AudioFile<double>::AudioBuffer modelInputAudioBuffer;
	modelInputAudioBuffer.resize(iNumberOfChanel);
	modelInputAudioBuffer[0].resize(inputBufferSize);

	// �Ӷ�����ȡ���������Ƶ����
	int iAudioBufferPos = 0;
	if (*lModelInputSampleBufferReadPos < lTmpModelInputSampleBufferWritePos) {
		for (int i = *lModelInputSampleBufferReadPos; i < lTmpModelInputSampleBufferWritePos; i++) {
			modelInputAudioBuffer[0][iAudioBufferPos++] = fModeulInputSampleBuffer[i];
		}
	}
	else {
		for (int i = *lModelInputSampleBufferReadPos; i < lModelInputOutputBufferSize; i++) {
			modelInputAudioBuffer[0][iAudioBufferPos++] = fModeulInputSampleBuffer[i];
		}
		for (int i = 0; i < lTmpModelInputSampleBufferWritePos; i++) {
			modelInputAudioBuffer[0][iAudioBufferPos++] = fModeulInputSampleBuffer[i];
		}
	}
	// ��ȡ��ϣ�����ָ��ָ�����дָ��
	*lModelInputSampleBufferReadPos = lTmpModelInputSampleBufferWritePos;

	AudioFile<double> audioFile;
	audioFile.shouldLogErrorsToConsole(true);
	audioFile.setAudioBuffer(modelInputAudioBuffer);
	audioFile.setAudioBufferSize(iNumberOfChanel, inputBufferSize);
	audioFile.setBitDepth(24);
	audioFile.setSampleRate(dProjectSampleRate);
	
	// ������Ƶ�ļ����ڴ�
	std::vector<uint8_t> vModelInputMemoryBuffer = std::vector<uint8_t>(0);
	audioFile.saveToWaveMemory(&vModelInputMemoryBuffer);

	// ����AIģ�ͽ�����������
	httplib::Client cli(roleStruct.sApiUrl);

	cli.set_connection_timeout(0, 1000000); // 300 milliseconds
	cli.set_read_timeout(5, 0); // 5 seconds
	cli.set_write_timeout(5, 0); // 5 seconds

	// ���ڴ��ȡ����
	auto vModelInputData = vModelInputMemoryBuffer.data();
	std::string sBuffer(vModelInputData, vModelInputData + vModelInputMemoryBuffer.size());

	auto sBufferSize = sBuffer.size();
	char buff[100];
	snprintf(buff, sizeof(buff), "�����ļ���С:%llu\n", sBufferSize);
	std::string buffAsStdStr = buff;
	OutputDebugStringA(buff);

	snprintf(buff, sizeof(buff), "%f", fPitchChange);
	std::string sCalcPitchError;
	if (bCalcPitchError) {
		sCalcPitchError = "true";
	}
	else {
		sCalcPitchError = "false";
	}

	char buffSamplerate[100];
	snprintf(buffSamplerate, sizeof(buffSamplerate), "%f", dProjectSampleRate);

	httplib::MultipartFormDataItems items = {
		{ "sSpeakId", roleStruct.sSpeakId, "", ""},
		{ "sName", roleStruct.sName, "", ""},
		{ "fPitchChange", buff, "", ""},
		{ "sampleRate", buffSamplerate, "", ""},
		{ "bCalcPitchError", sCalcPitchError.c_str(), "", ""},
		{ "sample", sBuffer, "sample.wav", "audio/x-wav"},
	};

	OutputDebugStringA("����AI�㷨ģ��\n");
	auto res = cli.Post("/voiceChangeModel", items);
	if (res.error() == httplib::Error::Success && res->status == 200) {
		// ���óɹ�����ʼ��������뵽��ʱ�����������滻���
		std::string body = res->body;
		std::vector<uint8_t> vModelOutputBuffer(body.begin(), body.end());

		AudioFile<double> tmpAudioFile;
		tmpAudioFile.loadFromMemory(vModelOutputBuffer);
		int sampleRate = tmpAudioFile.getSampleRate();
		int bitDepth = tmpAudioFile.getBitDepth();

		int numSamples = tmpAudioFile.getNumSamplesPerChannel();
		double lengthInSeconds = tmpAudioFile.getLengthInSeconds();

		int numChannels = tmpAudioFile.getNumChannels();
		bool isMono = tmpAudioFile.isMono();
		bool isStereo = tmpAudioFile.isStereo();

		// ��Ƶ��ʽ����
		// ��1s���������Ƕ������0.1s��ȡ�����������ƴ��
		
		// ����ǰ����Ƶ�ź�
		int iSkipSamplePosStart = fPrefixLength * sampleRate;
		// ����β���ź�
		int iSkipSamplePosEnd = numSamples - (fDropSuffixLength * sampleRate);
		int iSliceSampleNumber = iSkipSamplePosEnd - iSkipSamplePosStart;
		float* fSliceSampleBuffer = (float*)(std::malloc(sizeof(float) * iSliceSampleNumber));
		int iSlicePos = 0;
		auto fOriginAudioBuffer = tmpAudioFile.samples[0];
		for (int i = iSkipSamplePosStart; i < iSkipSamplePosEnd; i++) {
			fSliceSampleBuffer[iSlicePos++] = fOriginAudioBuffer[i];
		}

		// ��Ƶ�ز���
		float* fReSampleInBuffer = (float*)malloc(iSliceSampleNumber * sizeof(float));
		float* fReSampleOutBuffer = fReSampleInBuffer;
		int iResampleNumbers = iSliceSampleNumber;
		for (int i = 0; i < iSliceSampleNumber; i++) {
			fReSampleInBuffer[i] = fSliceSampleBuffer[i];
		}
		if (sampleRate != dProjectSampleRate) {
			double fScaleRate = dProjectSampleRate / sampleRate;
			iResampleNumbers = fScaleRate * iSliceSampleNumber;
			fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * (iResampleNumbers + 128)));
			func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, iSliceSampleNumber, iResampleNumbers);
		}

		// Ϊ�˱��ڼ�������һ���ظ�
		int iRepeatSilenceSampleNumber = dProjectSampleRate * fRepeatTime;

		long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
		for (int i = 0; i < iResampleNumbers; i++) {
			fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = fReSampleOutBuffer[i];
			if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
				lTmpModelOutputSampleBufferWritePos = 0;
			}
			// ע�⣬�˴�������дָ��׷�϶�ָ����������˻�����Ӧ�������ܵĴ�
		}
		if (bRepeat) {
			for (int i = 0; i < iRepeatSilenceSampleNumber; i++) {
				fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = 0.00001f;
				if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lTmpModelOutputSampleBufferWritePos = 0;
				}
			}
			for (int i = 0; i < iResampleNumbers; i++) {
				fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = fReSampleOutBuffer[i];
				if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lTmpModelOutputSampleBufferWritePos = 0;
				}
			}
		}
		// ��дָ��ָ���µ�λ��
		*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
	}
	else {
		auto err = res.error();
		char buff[100];
		snprintf(buff, sizeof(buff), "�㷨�������:%d\n", err);
		std::string buffAsStdStr = buff;
		OutputDebugStringA(buff);
	}
}

NetProcessProcessor::NetProcessProcessor()
	: kRecordState(IDLE)
	, bNeedContinueRecord(false)
	, fRecordIdleTime(0.f)
	// Ĭ��ֻ��������
	, iNumberOfChanel(1)
	, bRepeat(defaultEnableTwiceRepeat)
	, bCalcPitchError(defaultEnabelPitchErrorCalc)
	, fRepeatTime(0.f)
	, fMaxSliceLength(2.f)
	, fPitchChange(0.f)
	, dllFuncSrcSimple(nullptr){

	//--- set the wanted controller for our processor
	setControllerClass (kNetProcessControllerUID);
}

//------------------------------------------------------------------------
NetProcessProcessor::~NetProcessProcessor ()
{}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::initialize (FUnknown* context)
{
	std::wstring sDllDir = L"C:/Program Files/Common Files/VST3/NetProcess.vst3/Contents/x86_64-win";
	AddDllDirectory(sDllDir.c_str());
	sDllDir = L"C:/Windows/SysWOW64";
	AddDllDirectory(sDllDir.c_str());
	sDllDir = L"C:/temp/vst";
	AddDllDirectory(sDllDir.c_str());
	sDllDir = L"C:/temp/vst3";
	AddDllDirectory(sDllDir.c_str());

	std::wstring sDllPath = L"C:/Program Files/Common Files/VST3/NetProcess.vst3/Contents/x86_64-win/samplerate.dll";
	auto dllClient = LoadLibrary(sDllPath.c_str());
	if (dllClient != NULL) {
		dllFuncSrcSimple = (FUNC_SRC_SIMPLE)GetProcAddress(dllClient, "src_simple");
	}
	else {
		OutputDebugStringA("DLL load Error!");
	}

	// JSON�����ļ�
	std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
	std::stringstream buffer_pc_file;
	buffer_pc_file << t_pc_file.rdbuf();
	
	Json::Value jsonRoot;
	buffer_pc_file >> jsonRoot;
	int iRoleSize = jsonRoot["roleList"].size();

	fSampleVolumeWorkActiveVal = jsonRoot["fSampleVolumeWorkActiveVal"].asDouble();

	roleList.clear();
	for (int i = 0; i < iRoleSize; i++) {
		std::string apiUrl = jsonRoot["roleList"][i]["apiUrl"].asString();
		std::string name = jsonRoot["roleList"][i]["name"].asString();
		std::string speakId = jsonRoot["roleList"][i]["speakId"].asString();
		roleStruct role;
		role.sSpeakId = speakId;
		role.sName = name;
		role.sApiUrl = apiUrl;
		roleList.push_back(role);
	}
	iSelectRoleIndex = 0;

	// ǰ�������������ʼ��
	fPrefixLength = jsonRoot["fPrefixLength"].asFloat();
	fDropSuffixLength = jsonRoot["fDropSuffixLength"].asFloat();
	lPrefixLengthSampleNumber = fPrefixLength * this->processSetup.sampleRate;
	fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixLengthSampleNumber);
	lPrefixBufferPos = 0;

	// ��ʼ���̼߳佻�����ݵĻ�����
	float fModelInputOutputBufferSecond = 120.f;
	lModelInputOutputBufferSize = fModelInputOutputBufferSecond * this->processSetup.sampleRate;
	fModeulInputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	fModeulOutputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	lModelInputSampleBufferReadPos = 0;
	lModelInputSampleBufferWritePos = 0;
	lModelOutputSampleBufferReadPos = 0;
	lModelOutputSampleBufferWritePos = 0;

	//---always initialize the parent-------
	tresult result = AudioEffect::initialize (context);
	// if everything Ok, continue
	if (result != kResultOk)
	{
		return result;
	}

	//--- create Audio IO ------
	addAudioInput (STR16 ("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

	/* If you don't need an event bus, you can remove the next line */
	addEventInput (STR16 ("Event In"), 1);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::terminate ()
{
	// Here the Plug-in will be de-instanciated, last possibility to remove some memory!
	
	//---do not forget to call parent ------
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::setActive (TBool state)
{
	//--- called when the Plug-in is enable/disable (On/Off) -----
	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::process (Vst::ProcessData& data)
{
	//--- First : Read inputs parameter changes-----------

	
	// ��������仯
	if (data.inputParameterChanges)
	{
		int32 numParamsChanged = data.inputParameterChanges->getParameterCount();
		for (int32 index = 0; index < numParamsChanged; index++)
		{
			if (auto* paramQueue = data.inputParameterChanges->getParameterData(index))
			{
				Vst::ParamValue value;
				int32 sampleOffset;
				int32 numPoints = paramQueue->getPointCount();
				paramQueue->getPoint(numPoints - 1, sampleOffset, value);
				switch (paramQueue->getParameterId())
				{
				case kEnableTwiceRepeat:
					OutputDebugStringA("kEnableTwiceRepeat\n");
					bRepeat = (bool)value;
					break;
				case kEnabelPitchErrorCalc:
					OutputDebugStringA("kEnabelPitchErrorCalc\n");
					bCalcPitchError = (bool)value;
					break;
				case kTwiceRepeatIntvalTime:
					OutputDebugStringA("kTwiceRepeatIntvalTime\n");
					fRepeatTime = value * maxTwiceRepeatIntvalTime;
					break;
				case kMaxSliceLength:
					OutputDebugStringA("kMaxSliceLength\n");
					fMaxSliceLength = value * maxMaxSliceLength + 0.1f;
					lMaxSliceLengthSampleNumber = this->processSetup.sampleRate * fMaxSliceLength;
					break;
				case kPitchChange:
					OutputDebugStringA("kPitchChange\n");
					fPitchChange = value * (maxPitchChange - minPitchChange) + minPitchChange;
					break;
				case kSelectRole:
					OutputDebugStringA("kSelectRole\n");
					iSelectRoleIndex= std::min<int8>(
						(int8)(roleList.size() * value), roleList.size() - 1);
					break;
				}
			}
		}
	}

	//--- Here you have to implement your processing
	Vst::Sample32* inputL = data.inputs[0].channelBuffers32[0];
	//Vst::Sample32* inputR = data.inputs[0].channelBuffers32[1];
	Vst::Sample32* outputL = data.outputs[0].channelBuffers32[0];
	Vst::Sample32* outputR = data.outputs[0].channelBuffers32[1];
	double fSampleMax = -9999;
	for (int32 i = 0; i < data.numSamples; i++) {
		// ������˵��źŸ���һ��
		// modelInputAudioBuffer[0][lModelInputAudioBufferPos + i] = inputL[i];
		// modelInputAudioBuffer[1][lModelInputAudioBufferPos + i] = inputR[i];

		// ��ȡ��ǰ����������
		double fCurrentSample = inputL[i];
		double fSampleAbs = std::abs(fCurrentSample);
		if (fSampleAbs > fSampleMax) {
			fSampleMax = fSampleAbs;
		}

		// ����ǰ�źŸ��Ƶ�ǰ���źŻ�������
		fPrefixBuffer[lPrefixBufferPos++] = fCurrentSample;
		if (lPrefixBufferPos == lPrefixLengthSampleNumber) {
			lPrefixBufferPos = 0;
		}
	}
	lPrefixBufferPos = (lPrefixBufferPos + data.numSamples) % lPrefixLengthSampleNumber;


	char buff[100];
	snprintf(buff, sizeof(buff), "��ǰ��Ƶ���ݵ��������:%f\n", fSampleMax);
	std::string buffAsStdStr = buff;
	OutputDebugStringA(buff);

	bool bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;

	if (bVolumeDetectFine) {
		fRecordIdleTime = 0.f;
	}
	else {
		fRecordIdleTime += 1.f * data.numSamples / this->processSetup.sampleRate;
		char buff[100];
		snprintf(buff, sizeof(buff), "��ǰ�ۻ�����ʱ��:%f\n", fRecordIdleTime);
		std::string buffAsStdStr = buff;
		OutputDebugStringA(buff);
	}

	if (kRecordState == IDLE) {
		// ��ǰ�ǿ���״̬
		if (bNeedContinueRecord || bVolumeDetectFine) {
			OutputDebugStringA("�л�������״̬");
			kRecordState = WORK;
			// ����ǰ����Ƶ����д�뵽ģ����λ�������
			//mInputQueueMutex.lock();
			/*for (int32 i = 0; i < data.numSamples; i++) {
				qModelInputSampleQueue.push(inputL[i]);
			}*/
			for (int i = lPrefixBufferPos; i < lPrefixLengthSampleNumber; i++) {
				fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
				if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lModelInputSampleBufferWritePos = 0;
				}
				//qModelInputSampleQueue.push(fPrefixBuffer[i]);
			}
			for (int i = 0; i < lPrefixBufferPos; i++) {
				fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
				if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lModelInputSampleBufferWritePos = 0;
				}
				//qModelInputSampleQueue.push(fPrefixBuffer[i]);
			}
			//mInputQueueMutex.unlock();
		}
	}
	else {
		// ��ǰ�ǹ���״̬
		// ����ǰ����Ƶ����д�뵽ģ����λ�������
		//mInputQueueMutex.lock();
		for (int i = 0; i < data.numSamples; i++) {
			fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = inputL[i];
			if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
				lModelInputSampleBufferWritePos = 0;
			}
			//qModelInputSampleQueue.push(inputL[i]);
		}
		//mInputQueueMutex.unlock();
		// �ж��Ƿ���Ҫ�˳�����״̬
		bool bExitWorkState = false;

		// �˳�����2��������С�ҳ�������һ��ʱ��
		float fLowVolumeDetectTime = 1.f; // 1s
		if (fRecordIdleTime >= fLowVolumeDetectTime) {
			bExitWorkState = true;
			bNeedContinueRecord = false;
			OutputDebugStringA("������С�ҳ�������һ��ʱ�䣬ֱ�ӵ���ģ��\n");
		}

		// �˳�����3�����дﵽһ���Ĵ�С
		long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, lModelInputSampleBufferReadPos, lModelInputSampleBufferWritePos);
		if (inputBufferSize > lMaxSliceLengthSampleNumber + lPrefixLengthSampleNumber) {
			bExitWorkState = true;
			bNeedContinueRecord = true;
			OutputDebugStringA("���д�С�ﵽԤ�ڣ�ֱ�ӵ���ģ��\n");
		}

		if (bExitWorkState) {
			// ��Ҫ�˳�����״̬
			kRecordState = IDLE;
			std::thread (func_do_voice_transfer,
				iNumberOfChanel,
				this->processSetup.sampleRate,
				//&qModelInputSampleQueue,
				//&qModelOutputSampleQueue,
				lModelInputOutputBufferSize,
				fModeulInputSampleBuffer,
				&lModelInputSampleBufferReadPos,
				&lModelInputSampleBufferWritePos,
				fModeulOutputSampleBuffer,
				&lModelOutputSampleBufferReadPos,
				&lModelOutputSampleBufferWritePos,
				fPrefixLength,
				fDropSuffixLength,
				bRepeat,
				fRepeatTime,
				fPitchChange,
				bCalcPitchError,
				roleList[iSelectRoleIndex],
				dllFuncSrcSimple).detach();
		}
	}

	// ���ģ������������������ݵĻ���д�뵽����ź���ȥ
	int channel = 0;
	bool bHasRightChanel = true;
	if (outputR == outputL || outputR == NULL) bHasRightChanel = false;
	if (lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos) {
		OutputDebugStringA("ģ������������������ݵĻ���д�뵽����ź���ȥ\n");
		bool bFinish = false;
		//mOutputQueueMutex.lock();
		for (int i = 0; i < data.numSamples; i++)
		{
			bFinish = lModelOutputSampleBufferReadPos == lModelOutputSampleBufferWritePos;
			if (bFinish) {
				outputL[i] = 0.f;
				if (bHasRightChanel) {
					outputR[i] = 0.f;
				}
			}
			else {
				//double currentSample = qModelOutputSampleQueue.front();
				//qModelOutputSampleQueue.pop();
				double currentSample = fModeulOutputSampleBuffer[lModelOutputSampleBufferReadPos++];
				if (lModelOutputSampleBufferReadPos == lModelInputOutputBufferSize) {
					lModelOutputSampleBufferReadPos = 0;
				}
				outputL[i] = currentSample;
				if (bHasRightChanel) {
					outputR[i] = currentSample;
				}
			}
		}
		//mOutputQueueMutex.unlock();
		if (bFinish) {
			// ����ȡ����
			OutputDebugStringA("����ȡ����\n");
		}
	}
	else {
		for (int32 i = 0; i < data.numSamples; i++) {
			// ���������
			outputL[i] = 0.0000000001f;
			if (bHasRightChanel) {
				outputR[i] = 0.0000000001f;
			}
		}
	}
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::setupProcessing (Vst::ProcessSetup& newSetup)
{
	//--- called before any processing ----
	return AudioEffect::setupProcessing (newSetup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	// by default kSample32 is supported
	if (symbolicSampleSize == Vst::kSample32)
		return kResultTrue;

	// disable the following comment if your processing support kSample64
	/* if (symbolicSampleSize == Vst::kSample64)
		return kResultTrue; */

	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::setState (IBStream* state)
{
	// called when we load a preset, the model has to be reloaded
	IBStreamer streamer (state, kLittleEndian);
	bool bVal;
	float fVal;
	if (streamer.readBool(bVal) == false) {
		return kResultFalse;
	}
	bRepeat = bVal;
	if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fRepeatTime = fVal * maxTwiceRepeatIntvalTime;
	if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fMaxSliceLength = fVal * maxMaxSliceLength + 0.1f;
	lMaxSliceLengthSampleNumber = this->processSetup.sampleRate * fMaxSliceLength;
	if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fPitchChange = fVal * (maxPitchChange - minPitchChange) + minPitchChange;
	if (streamer.readBool(bVal) == false) {
		return kResultFalse;
	}
	bCalcPitchError = bVal;

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::getState (IBStream* state)
{
	// here we need to save the model
	// �������õ��־û��ļ���
	IBStreamer streamer(state, kLittleEndian);

	streamer.writeBool(bRepeat);
	streamer.writeFloat(fRepeatTime / maxTwiceRepeatIntvalTime);
	streamer.writeFloat((fMaxSliceLength - 0.1f) / maxMaxSliceLength);
	streamer.writeFloat((fPitchChange - minPitchChange) / (maxPitchChange - minPitchChange));
	streamer.writeBool(bCalcPitchError);
	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace MyCompanyName
