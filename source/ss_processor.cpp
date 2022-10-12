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
#include <mutex>
#include "params.h"
#include "json/json.h"

using namespace Steinberg;

namespace MyCompanyName {
//------------------------------------------------------------------------
// NetProcessProcessor
//------------------------------------------------------------------------



// ��������������Ϊ��ʱ���ڵ������߳�����У��������߳̿��ٱ���
	void func_do_voice_transfer(
		int iNumberOfChanel,									// ͨ������
		double dProjectSampleRate,								// ��Ŀ������
		//AudioFile<double>::AudioBuffer modelInputAudioBuffer,	// AIģ����λ�����
		//long maxInputBufferSize,								// ģ����λ�������С
		//long* lModelInputAudioBufferPos,						// ģ����λ�������дλ��ָ��
		//AudioFile<double>::AudioBuffer* modelOutputAudioBuffer,  // AIģ�ͳ��λ�����
		//long* lModelOutputAudioBufferPos,						// ģ�ͳ��λ�������дλ��ָ��
		std::string sSaveModelInputWaveFileName,				// ģ�͵���α���������ļ���
		std::string sSaveModelOutputWaveFileName,				// ģ�͵ķ��ؽ������������ļ���
		//bool* bHasMoreOutputData								// ���ر�ǣ���ʾ��ǰ��ģ�ͷ���
		std::queue<double>* qModelInputSampleQueue,				// ģ����ζ���
		std::queue<double>* qModelOutputSampleQueue,				// ģ�ͷ��ض���
		std::mutex* mIntputQueueMutex,
		std::mutex* mOutputQueueMutex,
		bool bRepeat,
		float fRepeatTime,
		float fPitchChange,
		//float fPrefixLength
		bool bCalcPitchError,
		roleStruct roleStruct
) {
	// ������Ƶ���ݵ��ļ�
	(*mIntputQueueMutex).lock();
	size_t inputQueueSize = (*qModelInputSampleQueue).size();

	AudioFile<double>::AudioBuffer modelInputAudioBuffer;
	modelInputAudioBuffer.resize(iNumberOfChanel);
	modelInputAudioBuffer[0].resize(inputQueueSize);

	// �Ӷ�����ȡ���������Ƶ����
	for (int i = 0; i < inputQueueSize; i++) {
		modelInputAudioBuffer[0][i] = (*qModelInputSampleQueue).front();
		(*qModelInputSampleQueue).pop();
	}
	(*mIntputQueueMutex).unlock();

	AudioFile<double> audioFile;
	audioFile.shouldLogErrorsToConsole(true);
	audioFile.setAudioBuffer(modelInputAudioBuffer);
	audioFile.setAudioBufferSize(iNumberOfChanel, inputQueueSize);
	audioFile.setBitDepth(24);
	audioFile.setSampleRate(dProjectSampleRate);
	audioFile.save(sSaveModelInputWaveFileName, AudioFileFormat::Wave);


	// ����AIģ�ͽ�����������
	//httplib::Client cli("http://192.168.3.253:6842");
	//httplib::Client cli("http://ros.bigf00t.net:6842");
	httplib::Client cli(roleStruct.sApiUrl);

	cli.set_connection_timeout(0, 1000000); // 300 milliseconds
	cli.set_read_timeout(5, 0); // 5 seconds
	cli.set_write_timeout(5, 0); // 5 seconds

	std::ifstream t_pc_file(sSaveModelInputWaveFileName, std::ios::binary);
	std::stringstream buffer_pc_file;
	buffer_pc_file << t_pc_file.rdbuf();
	auto sBuffer = buffer_pc_file.str();
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
		// д���ļ�
		std::ofstream t_out_file(sSaveModelOutputWaveFileName, std::ios::binary);
		std::string body = res->body;
		for (size_t i = 0; i < body.size(); i++)
		{
			char value = (char)body[i];
			t_out_file.write(&value, sizeof(char));
		}
		t_out_file.close();

		// ���ļ��ж�ȡ��Ƶ���ݣ����������������
		AudioFile<double> tmpAudioFile;
		tmpAudioFile.load(sSaveModelOutputWaveFileName);
		int sampleRate = tmpAudioFile.getSampleRate();
		int bitDepth = tmpAudioFile.getBitDepth();

		int numSamples = tmpAudioFile.getNumSamplesPerChannel();
		double lengthInSeconds = tmpAudioFile.getLengthInSeconds();

		int numChannels = tmpAudioFile.getNumChannels();
		bool isMono = tmpAudioFile.isMono();
		bool isStereo = tmpAudioFile.isStereo();

		// Ϊ�˱��ڼ�������һ���ظ�
		int iRepeatSampleNumber = dProjectSampleRate * fRepeatTime;
		// ����ǰ����Ƶ�ź�
		//int iSkipSamplePos = fPrefixLength * sampleRate;
		//iSkipSamplePos = 0;
		(*mOutputQueueMutex).lock();
		for (int i = 0; i < numSamples; i++) {
			(*qModelOutputSampleQueue).push(tmpAudioFile.samples[0][i]);
		}
		if (bRepeat) {
			for (int i = 0; i < iRepeatSampleNumber; i++) {
				(*qModelOutputSampleQueue).push(0.00001f);
			}
			for (int i = 0; i < numSamples; i++) {
				(*qModelOutputSampleQueue).push(tmpAudioFile.samples[0][i]);
			}
		}
		(*mOutputQueueMutex).unlock();
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
	: mBuffer(nullptr)
	, mBufferPos(0)
	//, modelInputAudioBuffer(0)
	//, lModelInputAudioBufferPos(0)
	//1000000Լ20��
	//500000Լ10��
	//200000Լ5��
	//100000Լ2��
	//, maxInputBufferSize(100000)
	, kRecordState(IDLE)
	, fRecordIdleTime(0.f)
	// Ĭ��ֻ��������
	, iNumberOfChanel(1)
	// ģ������ļ���ز���
	//, modelOutputAudioBuffer(0)
	//, lModelOutputAudioBufferPos(0)
	//, bHasMoreOutputData(false)
	//, qModelInputSampleQueue(0)
	//, qModelOutputSampleQueue(0)
	, bRepeat(defaultEnableTwiceRepeat)
	, bCalcPitchError(defaultEnabelPitchErrorCalc)
	, fRepeatTime(0.f)
	, fMaxSliceLength(2.f)
	, fPitchChange(0.f)
	//, fPrefixLength(0.01f)
	//, lPrefixBufferPos(0)
	//, fPrefixBuffer(nullptr)
{
	//--- set the wanted controller for our processor
	setControllerClass (kNetProcessControllerUID);
}

//------------------------------------------------------------------------
NetProcessProcessor::~NetProcessProcessor ()
{}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::initialize (FUnknown* context)
{
	// Here the Plug-in will be instanciated
	// ǰ���źŻ���
	/*lPrefixLengthSampleNumber = fPrefixLength * this->processSetup.sampleRate;
	fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixLengthSampleNumber);
	if (fPrefixBuffer) {
		memset(fPrefixBuffer, 0.f, lPrefixLengthSampleNumber);
	}*/


	// JSON�����ļ�
	std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
	std::stringstream buffer_pc_file;
	buffer_pc_file << t_pc_file.rdbuf();
	//auto sBuffer = buffer_pc_file.str();
	
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
					fMaxSliceLength = value * maxMaxSliceLength + 1.f;
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
				/*case kPrefixBufferLength:
					OutputDebugStringA("kPrefixBufferLength\n");
					fPrefixLength = value * maxPrefixBufferLength + minPrefixBufferLength;
					lPrefixLengthSampleNumber = fPrefixLength * this->processSetup.sampleRate;
					if (fPrefixBuffer) {
						std::free(fPrefixBuffer);
					}
					fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixLengthSampleNumber);
					if (fPrefixBuffer) {
						memset(fPrefixBuffer, 0.f, lPrefixLengthSampleNumber);
					}
					lPrefixBufferPos = 0;
					break;*/
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
		//fSampleSum += inputL[i] + inputR[i];

		// ������˶��źŽ��зŴ�
		// outputL[i] = inputL[i] * 2;
		// outputR[i] = inputR[i] * 2;

		// ���������
		//outputL[i] = 0.0000000001f;
		//outputR[i] = 0;

		// ����ǰ�źŸ��Ƶ�ǰ���źŻ�������
		//fPrefixBuffer[(lPrefixBufferPos + i) % lPrefixLengthSampleNumber] = fCurrentSample;
	}
	//lPrefixBufferPos = (lPrefixBufferPos + data.numSamples) % lPrefixLengthSampleNumber;

	char buff[100];
	snprintf(buff, sizeof(buff), "��ǰ��Ƶ���ݵ��������:%f\n", fSampleMax);
	std::string buffAsStdStr = buff;
	OutputDebugStringA(buff);

	// double fSampleVolumeWorkActiveVal = 0.05;
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
		if (bVolumeDetectFine) {
			OutputDebugStringA("�л�������״̬");
			kRecordState = WORK;
			// ����ǰ����Ƶ����д�뵽ģ����λ�������
			mInputQueueMutex.lock();
			for (int32 i = 0; i < data.numSamples; i++) {
				qModelInputSampleQueue.push(inputL[i]);
				//modelInputAudioBuffer[0][lModelInputAudioBufferPos + i] = inputL[i];
				//modelInputAudioBuffer[1][lModelInputAudioBufferPos + i] = inputR[i];
			}
			// ��Ϊ������ǰ�����棬��������ֱ�Ӵ�ǰ��������ȡ��Ƶ���ݣ�ǰ������Ĵ�СӦ���ܰ����µ�ǰ�Ļ���������
			// �൱���ڿ�ʼ������ʱ���ֱ�Ӵ���ǰ�����������
			/*for (int i = lPrefixBufferPos; i < lPrefixLengthSampleNumber; i++) {
				qModelInputSampleQueue.push(fPrefixBuffer[i]);
			}
			for (int i = 0; i < lPrefixBufferPos; i++) {
				qModelInputSampleQueue.push(fPrefixBuffer[i]);
			}*/
			mInputQueueMutex.unlock();
			//lModelInputAudioBufferPos += data.numSamples;
		}
	}
	else {
		// ��ǰ�ǹ���״̬
		// ����ǰ����Ƶ����д�뵽ģ����λ�������
		mInputQueueMutex.lock();
		for (int32 i = 0; i < data.numSamples; i++) {
			qModelInputSampleQueue.push(inputL[i]);
			// modelInputAudioBuffer[0][lModelInputAudioBufferPos + i] = inputL[i];
			//modelInputAudioBuffer[1][lModelInputAudioBufferPos + i] = inputR[i];
		}
		mInputQueueMutex.unlock();
		// lModelInputAudioBufferPos += data.numSamples;
		// �ж��Ƿ���Ҫ�˳�����״̬
		bool bExitWorkState = false;
		// �˳�����1����������������֧����һ��д���ʱ��
		/*if (lModelInputAudioBufferPos + data.numSamples > maxInputBufferSize) {
			bExitWorkState = true;
			OutputDebugStringA("��������������֧����һ��д���ʱ��ֱ�ӵ���ģ��\n");
		}*/

		// �˳�����2��������С�ҳ�������һ��ʱ��
		float fLowVolumeDetectTime = 1.f; // 1s
		if (fRecordIdleTime >= fLowVolumeDetectTime) {
			bExitWorkState = true;
			OutputDebugStringA("������С�ҳ�������һ��ʱ�䣬ֱ�ӵ���ģ��\n");
		}

		// �˳�����3�����дﵽһ���Ĵ�С
		//1000000Լ20��
		//500000Լ10��
		//200000Լ5��
		//100000Լ2��
		//if (qModelInputSampleQueue.size() > lMaxSliceLengthSampleNumber + lPrefixLengthSampleNumber) {
		if (qModelInputSampleQueue.size() > lMaxSliceLengthSampleNumber) {
			bExitWorkState = true;
			OutputDebugStringA("���д�С�ﵽԤ�ڣ�ֱ�ӵ���ģ��\n");
		}


		if (bExitWorkState) {
			// ��Ҫ�˳�����״̬
			kRecordState = IDLE;
			std::thread (func_do_voice_transfer,
				iNumberOfChanel,
				this->processSetup.sampleRate,
				//modelInputAudioBuffer,
				//maxInputBufferSize,
				//&lModelInputAudioBufferPos,
				//&modelOutputAudioBuffer,
				//&lModelOutputAudioBufferPos,
				sDefaultSaveModelInputWaveFileName,
				sDefaultSaveModelOutputWaveFileName,
				&qModelInputSampleQueue,
				&qModelOutputSampleQueue,
				&mInputQueueMutex,
				&mOutputQueueMutex,
				bRepeat,
				fRepeatTime,
				fPitchChange,
				bCalcPitchError,
				roleList[iSelectRoleIndex]).detach();
		}
	}

	// ���ģ������������������ݵĻ���д�뵽����ź���ȥ
	int channel = 0;
	bool bHasRightChanel = true;
	if (outputR == outputL || outputR == NULL) bHasRightChanel = false;
	if (!qModelOutputSampleQueue.empty()) {
		OutputDebugStringA("ģ������������������ݵĻ���д�뵽����ź���ȥ\n");
		bool bFinish = false;
		mOutputQueueMutex.lock();
		for (int i = 0; i < data.numSamples; i++)
		{
			/*int index = lModelOutputAudioBufferPos + i / 2;
			bFinish = index >= modelOutputAudioBuffer[channel].size();
			if (!bFinish) {
				double currentSample = modelOutputAudioBuffer[channel][index];
				outputL[i] = currentSample;
			}
			else {
				outputL[i] = 0.f;
			}*/

			bFinish = qModelOutputSampleQueue.empty();
			if (bFinish) {
				outputL[i] = 0.f;
				if (bHasRightChanel) {
					outputR[i] = 0.f;
				}
			}
			else {
				double currentSample = qModelOutputSampleQueue.front();
				qModelOutputSampleQueue.pop();
				/*if (i % 2 == 1) {
					qModelOutputSampleQueue.pop();
				}*/
				outputL[i] = currentSample;
				if (bHasRightChanel) {
					outputR[i] = currentSample;
				}
			}

		}
		mOutputQueueMutex.unlock();
		if (bFinish) {
			// ����ȡ����
			OutputDebugStringA("����ȡ����\n");
			//bHasMoreOutputData = false;
			//lModelOutputAudioBufferPos = 0;
		}
		//else {
		//	lModelOutputAudioBufferPos += data.numSamples / 2;
		//}
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
	fMaxSliceLength = fVal * maxMaxSliceLength + 1.f;
	lMaxSliceLengthSampleNumber = this->processSetup.sampleRate * fMaxSliceLength;
	if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fPitchChange = fVal * (maxPitchChange - minPitchChange) + minPitchChange;
	if (streamer.readBool(bVal) == false) {
		return kResultFalse;
	}
	bCalcPitchError = bVal;
	/*if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fPrefixLength = fVal * maxPrefixBufferLength + minPrefixBufferLength;
	lPrefixLengthSampleNumber = fPrefixLength * this->processSetup.sampleRate;
	fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixLengthSampleNumber);
	lPrefixBufferPos = 0;*/

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
	streamer.writeFloat((fMaxSliceLength - 1.f) / maxMaxSliceLength);
	streamer.writeFloat((fPitchChange - minPitchChange) / (maxPitchChange - minPitchChange));
	streamer.writeBool(bCalcPitchError);
	//streamer.writeFloat((fPrefixLength - minPrefixBufferLength) / maxPrefixBufferLength);
	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace MyCompanyName
