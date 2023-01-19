/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AudioFile.h"
#include "httplib.h"
using namespace std::chrono;

long long func_get_timestamp() {
    return (duration_cast<milliseconds>(system_clock::now().time_since_epoch())).count();
}

// �ز���
int func_audio_resample(FUNC_SRC_SIMPLE dllFuncSrcSimple, float* fInBuffer, float* fOutBuffer, double src_ratio, long lInSize, long lOutSize) {
	SRC_DATA data;
	data.src_ratio = src_ratio;
	data.input_frames = lInSize;
	data.output_frames = lOutSize;
	data.data_in = fInBuffer;
	data.data_out = fOutBuffer;
	int error = dllFuncSrcSimple(&data, SRC_SINC_FASTEST, 1);
	return error;
}

// ���ڼ���һ����д���������Ч���ݴ�С
long func_cacl_read_write_buffer_data_size(long lBufferSize, long lReadPos, long lWritePos) {
	long inputBufferSize;
	if (lReadPos == lWritePos) {
		return 0;
	}
	if (lReadPos < lWritePos) {
		inputBufferSize = lWritePos - lReadPos;
	}
	else {
		inputBufferSize = lWritePos + lBufferSize - lReadPos;
	}
	return inputBufferSize;
}

// boolֵ���л�Ϊ�ַ���
std::string func_bool_to_string(bool bVal) {
	if (bVal) {
		return "true";
	}
	else {
		return "false";
	}
}


// ��������������Ϊ��ʱ���ڵ������߳�����У��������߳̿��ٱ���
void func_do_voice_transfer_worker(
	int iNumberOfChanel,					// ͨ������
	double dProjectSampleRate,				// ��Ŀ������

	long lModelInputOutputBufferSize,		// ģ�����������������С
	float* fModeulInputSampleBuffer,		// ģ�����뻺����
	long* lModelInputSampleBufferReadPos,	// ģ�����뻺������ָ��
	long* lModelInputSampleBufferWritePos,	// ģ�����뻺����дָ��

	float* fModeulOutputSampleBuffer,		// ģ�����������
	long* lModelOutputSampleBufferReadPos,	// ģ�������������ָ��
	long* lModelOutputSampleBufferWritePos,	// ģ�����������дָ��

	float* fPrefixLength,					// ǰ��������ʱ��(s)
	float* fDropSuffixLength,				// ������β��ʱ��(s)
	float* fPitchChange,					// �����仯��ֵ
	bool* bCalcPitchError,					// �������������

	std::vector<roleStruct> roleStructList,	// ���õĿ�����ɫ�б�
	int* iSelectRoleIndex,					// ѡ��Ľ�ɫID
	FUNC_SRC_SIMPLE dllFuncSrcSimple,		// DLL�ڲ�SrcSimple����

	bool* bEnableSOVITSPreResample,			// ����SOVITSģ�������Ƶ�ز���Ԥ����
	int iSOVITSModelInputSamplerate,		// SOVITSģ����β�����
	bool* bEnableHUBERTPreResample,			// ����HUBERTģ�������Ƶ�ز���Ԥ����
	int iHUBERTInputSampleRate,				// HUBERTģ����β�����

	bool* bRealTimeModel,					// ռλ����ʵʱģʽ
	bool* bDoItSignal,						// ռλ������ʾ��worker�д����������
	bool* bEnableDebug,

	bool* bWorkerNeedExit					// ռλ������ʾworker�߳���Ҫ�˳�
	//std::mutex* mWorkerSafeExit				// ����������ʾworker�߳��Ѿ���ȫ�˳�
) {
	char buff[100];
	long long tTime1;
	long long tTime2;
	long long tUseTime;
	long long tStart;

	double dSOVITSInputSamplerate;
	char sSOVITSSamplerateBuff[100];
	char cPitchBuff[100];
	std::string sHUBERTSampleBuffer;
	std::string sCalcPitchError;
	std::string sEnablePreResample;

	//mWorkerSafeExit->lock();
	while (!*bWorkerNeedExit) {
		// ��ѵ����־λ
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (*bDoItSignal) {
			// ����Ҫ������źţ���ʼ����������־λ����Ϊfalse
			// �˴����ǵ��������⣬��δʹ�û�����ʵ��ԭ�Ӳ���
			// ��ͬһʱ���������Ҫ������źţ���ȵ���һ����־λΪtrueʱ�ٴ���Ҳ�޷�
			*bDoItSignal = false;
			tStart = func_get_timestamp();
			tTime1 = tStart;


			roleStruct roleStruct = roleStructList[*iSelectRoleIndex];

			// ������Ƶ���ݵ��ļ�
			// ��ȡ��ǰдָ���λ��
			long lTmpModelInputSampleBufferWritePos = *lModelInputSampleBufferWritePos;

			AudioFile<double>::AudioBuffer modelInputAudioBuffer;
			modelInputAudioBuffer.resize(iNumberOfChanel);

			// �Ӷ�����ȡ���������Ƶ����
			std::vector<float> vModelInputSampleBufferVector;
			if (*lModelInputSampleBufferReadPos < lTmpModelInputSampleBufferWritePos) {
				for (int i = *lModelInputSampleBufferReadPos; i < lTmpModelInputSampleBufferWritePos; i++) {
					vModelInputSampleBufferVector.push_back(fModeulInputSampleBuffer[i]);
				}
			}
			else {
				for (int i = *lModelInputSampleBufferReadPos; i < lModelInputOutputBufferSize; i++) {
					vModelInputSampleBufferVector.push_back(fModeulInputSampleBuffer[i]);
				}
				for (int i = 0; i < lTmpModelInputSampleBufferWritePos; i++) {
					vModelInputSampleBufferVector.push_back(fModeulInputSampleBuffer[i]);
				}
			}
			// ��ȡ��ϣ�����ָ��ָ�����дָ��
			*lModelInputSampleBufferReadPos = lTmpModelInputSampleBufferWritePos;


			if (*bEnableSOVITSPreResample) {
				// ��ǰ����Ƶ�ز�����C++�ز�����Python�˿�
				dSOVITSInputSamplerate = iSOVITSModelInputSamplerate;

				// SOVITS������Ƶ�ز���
				float* fReSampleInBuffer = vModelInputSampleBufferVector.data();
				float* fReSampleOutBuffer = fReSampleInBuffer;
				long iResampleNumbers = static_cast<long>(vModelInputSampleBufferVector.size());

				if (dProjectSampleRate != iSOVITSModelInputSamplerate) {
					double fScaleRate = iSOVITSModelInputSamplerate / dProjectSampleRate;
					iResampleNumbers = static_cast<long>(fScaleRate * iResampleNumbers);
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, static_cast<long>(vModelInputSampleBufferVector.size()), iResampleNumbers);
				}

				snprintf(sSOVITSSamplerateBuff, sizeof(sSOVITSSamplerateBuff), "%d", iSOVITSModelInputSamplerate);
				modelInputAudioBuffer[0].resize(iResampleNumbers);
				for (int i = 0; i < iResampleNumbers; i++) {
					modelInputAudioBuffer[0][i] = fReSampleOutBuffer[i];
				}

				if (*bEnableHUBERTPreResample) {
					// HUBERT������Ƶ�ز���
					double fScaleRate = iHUBERTInputSampleRate / dProjectSampleRate;
					iResampleNumbers = static_cast<long>(fScaleRate * vModelInputSampleBufferVector.size());
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, static_cast<long>(vModelInputSampleBufferVector.size()), iResampleNumbers);

					AudioFile<double>::AudioBuffer HUBERTModelInputAudioBuffer;
					HUBERTModelInputAudioBuffer.resize(iNumberOfChanel);
					HUBERTModelInputAudioBuffer[0].resize(iResampleNumbers);
					for (int i = 0; i < iResampleNumbers; i++) {
						HUBERTModelInputAudioBuffer[0][i] = fReSampleOutBuffer[i];
					}
					AudioFile<double> HUBERTAudioFile;
					HUBERTAudioFile.shouldLogErrorsToConsole(false);
					HUBERTAudioFile.setAudioBuffer(HUBERTModelInputAudioBuffer);
					HUBERTAudioFile.setAudioBufferSize(iNumberOfChanel, static_cast<int>(HUBERTModelInputAudioBuffer[0].size()));
					HUBERTAudioFile.setBitDepth(24);
					HUBERTAudioFile.setSampleRate(iHUBERTInputSampleRate);

					// ������Ƶ�ļ����ڴ�
					std::vector<uint8_t> vHUBERTModelInputMemoryBuffer;
					HUBERTAudioFile.saveToWaveMemory(&vHUBERTModelInputMemoryBuffer);

					// ���ڴ��ȡ����
					auto vHUBERTModelInputData = vHUBERTModelInputMemoryBuffer.data();
					std::string sHUBERTModelInputString(vHUBERTModelInputData, vHUBERTModelInputData + vHUBERTModelInputMemoryBuffer.size());
					sHUBERTSampleBuffer = sHUBERTModelInputString;
				}
				else {
					sHUBERTSampleBuffer = "";
				}
			}
			else {
				// δ����Ԥ�����ز�������Ƶԭ������
				sHUBERTSampleBuffer = "";
				dSOVITSInputSamplerate = dProjectSampleRate;
				snprintf(sSOVITSSamplerateBuff, sizeof(sSOVITSSamplerateBuff), "%f", dProjectSampleRate);
				modelInputAudioBuffer[0].resize(vModelInputSampleBufferVector.size());
				for (int i = 0; i < vModelInputSampleBufferVector.size(); i++) {
					modelInputAudioBuffer[0][i] = vModelInputSampleBufferVector[i];
				}
			}

			long iModelInputNumSamples = static_cast<long>(modelInputAudioBuffer[0].size());
			AudioFile<double> audioFile;
			audioFile.shouldLogErrorsToConsole(false);
			audioFile.setAudioBuffer(modelInputAudioBuffer);
			audioFile.setAudioBufferSize(iNumberOfChanel, iModelInputNumSamples);
			audioFile.setBitDepth(24);
			audioFile.setSampleRate(static_cast<UINT32>(dSOVITSInputSamplerate));

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "׼�����浽��Ƶ�ļ���ʱ:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
			tTime1 = tTime2;

			// ������Ƶ�ļ����ڴ�
			std::vector<uint8_t> vModelInputMemoryBuffer;
			audioFile.saveToWaveMemory(&vModelInputMemoryBuffer);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "���浽��Ƶ�ļ���ʱ:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
			tTime1 = tTime2;

			// ����AIģ�ͽ�����������
			httplib::Client cli(roleStruct.sApiUrl);

			cli.set_connection_timeout(0, 1000000); // 300 milliseconds
			cli.set_read_timeout(5, 0); // 5 seconds
			cli.set_write_timeout(5, 0); // 5 seconds

			// ���ڴ��ȡ����
			auto vModelInputData = vModelInputMemoryBuffer.data();
			std::string sModelInputString(vModelInputData, vModelInputData + vModelInputMemoryBuffer.size());

			// ׼��HTTP�������
			snprintf(cPitchBuff, sizeof(cPitchBuff), "%f", *fPitchChange);
			sCalcPitchError = func_bool_to_string(*bCalcPitchError);
			sEnablePreResample = func_bool_to_string(*bEnableSOVITSPreResample);

			httplib::MultipartFormDataItems items = {
				{ "sSpeakId", roleStruct.sSpeakId, "", ""},
				{ "sName", roleStruct.sName, "", ""},
				{ "fPitchChange", cPitchBuff, "", ""},
				{ "sampleRate", sSOVITSSamplerateBuff, "", ""},
				{ "bCalcPitchError", sCalcPitchError.c_str(), "", ""},
				{ "bEnablePreResample", sEnablePreResample.c_str(), "", ""},
				{ "sample", sModelInputString, "sample.wav", "audio/x-wav"},
				{ "hubert_sample", sHUBERTSampleBuffer, "hubert_sample.wav", "audio/x-wav"},
			};
			if (*bEnableDebug) {
				OutputDebugStringA("����AI�㷨ģ��\n");
			}
			auto res = cli.Post("/voiceChangeModel", items);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "����HTTP�ӿں�ʱ:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
			tTime1 = tTime2;

			if (res.error() == httplib::Error::Success && res->status == 200) {
				// ���óɹ�����ʼ��������뵽��ʱ�����������滻���
				std::string body = res->body;
				std::vector<uint8_t> vModelOutputBuffer(body.begin(), body.end());

				AudioFile<double> tmpAudioFile;
				tmpAudioFile.loadFromMemory(vModelOutputBuffer);
				int sampleRate = tmpAudioFile.getSampleRate();
				// int bitDepth = tmpAudioFile.getBitDepth();
				int numSamples = tmpAudioFile.getNumSamplesPerChannel();
				//double lengthInSeconds = tmpAudioFile.getLengthInSeconds();
				// int numChannels = tmpAudioFile.getNumChannels();
				//bool isMono = tmpAudioFile.isMono();

				// ��Ƶ��ʽ����
				// ��1s���������Ƕ������0.1s��ȡ�����������ƴ��

				// ����ǰ����Ƶ�ź�
				int iSkipSamplePosStart = static_cast<int>(*fPrefixLength * sampleRate);
				// ����β���ź�
				int iSkipSamplePosEnd = static_cast<int>(numSamples - (*fDropSuffixLength * sampleRate));
				int iSliceSampleNumber = iSkipSamplePosEnd - iSkipSamplePosStart;
				float* fSliceSampleBuffer = (float*)(std::malloc(sizeof(float) * iSliceSampleNumber));
				int iSlicePos = 0;
				std::vector<double> fOriginAudioBuffer = tmpAudioFile.samples[0];
				for (int i = iSkipSamplePosStart; i < iSkipSamplePosEnd; i++) {
					fSliceSampleBuffer[iSlicePos++] = static_cast<float>(fOriginAudioBuffer[i]);
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
					iResampleNumbers = static_cast<int>(fScaleRate * iSliceSampleNumber);
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, iSliceSampleNumber, iResampleNumbers);
				}

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "��ģ������ز�����ʱ:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}
				tTime1 = tTime2;

				// ��������ʵʱģʽʱ����������дָ����Ҫ���⴦��
				// ���磺�������ӳٶ���ʱ�����յ����µ�����ʱ���������о����ݣ���ʱֱ�Ӷ��������ݣ��������ݸ���
				if (*bRealTimeModel) {
					// ��ȫ����С����Ϊ���ڶ�д�̲߳����̰߳�ȫ�������������һ����ȫ����С���������ݳ�������
					int iRealTimeModeBufferSafeZoneSize = 16;
					// ������µ�дָ��λ�ã�
					// 1.��ǰ�����ݴ�С > ��ȫ����С��дָ��ǰ�ƶ�λ�ڰ�ȫ��β��
					// 2.��ǰ�����ݴ�С < ��ȫ����С��дָ��ǰ�ƶ�λ�ھ�����β�������κβ��������ֲ��䣩
					long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, *lModelOutputSampleBufferReadPos, *lModelOutputSampleBufferWritePos);
					if (inputBufferSize > iRealTimeModeBufferSafeZoneSize) {
						*lModelOutputSampleBufferWritePos = (*lModelOutputSampleBufferReadPos + iRealTimeModeBufferSafeZoneSize) % lModelInputOutputBufferSize;
					}
				}

				// ��дָ���ǵĻ�����λ�ÿ�ʼд���µ���Ƶ����
				long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;

				for (int i = 0; i < iResampleNumbers; i++) {
					fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = fReSampleOutBuffer[i];
					if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
						lTmpModelOutputSampleBufferWritePos = 0;
					}
					// ע�⣬��Ϊ������Ӧ�������ܵĴ����Դ˴�������дָ��׷�϶�ָ������
				}
				// ��дָ��ָ���µ�λ��
				*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "���дָ��:%ld\n", lTmpModelOutputSampleBufferWritePos);
					OutputDebugStringA(buff);
				}

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "д��������ʱ:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}
				tTime1 = tTime2;
			}
			else {
				auto err = res.error();
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "�㷨�������:%d\n", err);
					OutputDebugStringA(buff);
				}
			}
			tUseTime = func_get_timestamp() - tStart;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "�ô�woker��ѵ�ܺ�ʱ:%lld\n", tUseTime);
				OutputDebugStringA(buff);
			}
		}
	}
	//mWorkerSafeExit->unlock();
}

//==============================================================================
NetProcessJUCEVersionAudioProcessor::NetProcessJUCEVersionAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

NetProcessJUCEVersionAudioProcessor::~NetProcessJUCEVersionAudioProcessor()
{
}

//==============================================================================
const juce::String NetProcessJUCEVersionAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NetProcessJUCEVersionAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool NetProcessJUCEVersionAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool NetProcessJUCEVersionAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double NetProcessJUCEVersionAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NetProcessJUCEVersionAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int NetProcessJUCEVersionAudioProcessor::getCurrentProgram()
{
    return 0;
}

void NetProcessJUCEVersionAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String NetProcessJUCEVersionAudioProcessor::getProgramName (int index)
{
    return {};
}

void NetProcessJUCEVersionAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void NetProcessJUCEVersionAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
}

void NetProcessJUCEVersionAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NetProcessJUCEVersionAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void NetProcessJUCEVersionAudioProcessor::processBlock (juce::AudioBuffer<float>& audioBuffer, juce::MidiBuffer& midiMessages)
{
	if (initDone) {
		juce::ScopedNoDenormals noDenormals;
		auto totalNumInputChannels = getTotalNumInputChannels();
		auto totalNumOutputChannels = getTotalNumOutputChannels();

		// In case we have more outputs than inputs, this code clears any output
		// channels that didn't contain input data, (because these aren't
		// guaranteed to be empty - they may contain garbage).
		// This is here to avoid people getting screaming feedback
		// when they first compile a plugin, but obviously you don't need to keep
		// this code if your algorithm always overwrites all the output channels.
		for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
			audioBuffer.clear(i, 0, audioBuffer.getNumSamples());
		}

		char buff[100];

		double fSampleMax = -9999;
		float* inputOutputL = audioBuffer.getWritePointer(0);
		float* inputOutputR = NULL;
		bool bHasRightChanel = false;
		if (getNumOutputChannels() > 1) {
			inputOutputR = audioBuffer.getWritePointer(1);
			bHasRightChanel = true;
		}
		else {
			inputOutputR = inputOutputL;
		}
		for (juce::int32 i = 0; i < audioBuffer.getNumSamples(); i++) {
			// ��ȡ��ǰ����������
			float fCurrentSample = inputOutputL[i];
			float fSampleAbs = std::abs(fCurrentSample);
			if (fSampleAbs > fSampleMax) {
				fSampleMax = fSampleAbs;
			}

			// ����ǰ�źŸ��Ƶ�ǰ���źŻ�������
			fPrefixBuffer[lPrefixBufferPos++] = fCurrentSample;
			if (lPrefixBufferPos == lPrefixLengthSampleNumber) {
				lPrefixBufferPos = 0;
			}
		}
		lPrefixBufferPos = (lPrefixBufferPos + audioBuffer.getNumSamples()) % lPrefixLengthSampleNumber;

		if (bRealTimeMode) {
			// ���������ʵʱģʽ����������������ֵ
			bVolumeDetectFine = true;
		}
		else {
			bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;
		}

		if (bVolumeDetectFine) {
			fRecordIdleTime = 0.f;
		}
		else {
			fRecordIdleTime += 1.f * audioBuffer.getNumSamples() / getSampleRate();
			if (bEnableDebug) {
				snprintf(buff, sizeof(buff), "��ǰ�ۻ�����ʱ��:%f\n", fRecordIdleTime);
				OutputDebugStringA(buff);
			}
		}

		if (kRecordState == IDLE) {
			// ��ǰ�ǿ���״̬
			if (bVolumeDetectFine) {
				if (bEnableDebug) {
					OutputDebugStringA("�л�������״̬");
				}
				kRecordState = WORK;
				// ����ǰ����Ƶ����д�뵽ģ����λ�������
				for (int i = lPrefixBufferPos; i < lPrefixLengthSampleNumber; i++) {
					fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
					if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
						lModelInputSampleBufferWritePos = 0;
					}
				}
				for (int i = 0; i < lPrefixBufferPos; i++) {
					fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
					if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
						lModelInputSampleBufferWritePos = 0;
					}
				}
			}
		}
		else {
			// ��ǰ�ǹ���״̬
			// ����ǰ����Ƶ����д�뵽ģ����λ�������
			for (int i = 0; i < audioBuffer.getNumSamples(); i++) {
				fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = inputOutputL[i];
				if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lModelInputSampleBufferWritePos = 0;
				}
			}

			// �ж��Ƿ���Ҫ�˳�����״̬
			bool bExitWorkState = false;

			// �˳�����1��������С�ҳ�������һ��ʱ��
			if (fRecordIdleTime >= fLowVolumeDetectTime) {
				bExitWorkState = true;
				if (bEnableDebug) {
					OutputDebugStringA("������С�ҳ�������һ��ʱ�䣬ֱ�ӵ���ģ��\n");
				}
			}

			// �˳�����2�����дﵽһ���Ĵ�С
			long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, lModelInputSampleBufferReadPos, lModelInputSampleBufferWritePos);
			if (inputBufferSize > lMaxSliceLengthSampleNumber + lPrefixLengthSampleNumber) {
				bExitWorkState = true;
				if (bEnableDebug) {
					OutputDebugStringA("���д�С�ﵽԤ�ڣ�ֱ�ӵ���ģ��\n");
				}
			}

			if (bExitWorkState) {
				// ��Ҫ�˳�����״̬
				kRecordState = IDLE;
				// worker��־λ����Ϊtrue����worker���
				bDoItSignal = true;
			}
		}

		// ���ģ������������������ݵĻ���д�뵽����ź���ȥ
		if (bEnableDebug) {
			snprintf(buff, sizeof(buff), "�����ָ��:%ld\n", lModelOutputSampleBufferReadPos);
			OutputDebugStringA(buff);
		}
		if (lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos) {
			bool bFinish = false;
			for (int i = 0; i < audioBuffer.getNumSamples(); i++)
			{
				bFinish = lModelOutputSampleBufferReadPos == lModelOutputSampleBufferWritePos;
				if (bFinish) {
					inputOutputL[i] = 0.f;
					if (bHasRightChanel) {
						inputOutputR[i] = 0.f;
					}
				}
				else {
					double currentSample = fModeulOutputSampleBuffer[lModelOutputSampleBufferReadPos++];
					if (lModelOutputSampleBufferReadPos == lModelInputOutputBufferSize) {
						lModelOutputSampleBufferReadPos = 0;
					}
					inputOutputL[i] = static_cast<float>(currentSample);
					if (bHasRightChanel) {
						inputOutputR[i] = static_cast<float>(currentSample);
					}
				}
			}
			if (bFinish) {
				// ����ȡ����
				if (bEnableDebug) {
					OutputDebugStringA("����ȡ����\n");
				}
			}
		}
		else {
			lNoOutputCount += 1;
			if (bEnableDebug) {
				snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!��������:%ld\n", lNoOutputCount);
				OutputDebugStringA(buff);
			}
			for (juce::int32 i = 0; i < audioBuffer.getNumSamples(); i++) {
				// ���������
				inputOutputL[i] = 0.0000000001f;
				if (bHasRightChanel) {
					inputOutputR[i] = 0.0000000001f;
				}
			}
		}
	}
}

//==============================================================================
bool NetProcessJUCEVersionAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* NetProcessJUCEVersionAudioProcessor::createEditor()
{
    return new NetProcessJUCEVersionAudioProcessorEditor (*this);
}

//==============================================================================
void NetProcessJUCEVersionAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
	std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("Config"));
	xml->setAttribute("fMaxSliceLength", (double)fMaxSliceLength);
	xml->setAttribute("fMaxSliceLengthForRealTimeMode", (double)fMaxSliceLengthForRealTimeMode);
	xml->setAttribute("fMaxSliceLengthForSentenceMode", (double)fMaxSliceLengthForSentenceMode);
	xml->setAttribute("fLowVolumeDetectTime", (double)fLowVolumeDetectTime);
	xml->setAttribute("fPitchChange", (double)fPitchChange);
	xml->setAttribute("bRealTimeMode", bRealTimeMode);
	xml->setAttribute("bEnableDebug", bEnableDebug);
	xml->setAttribute("iSelectRoleIndex", iSelectRoleIndex);
	copyXmlToBinary(*xml, destData);
}

void NetProcessJUCEVersionAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.

	// default value
	fMaxSliceLength = 1.0;
	fMaxSliceLengthForRealTimeMode = fMaxSliceLength;
	fMaxSliceLengthForSentenceMode = fMaxSliceLength;
	fLowVolumeDetectTime = 0.4;
	lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
	fPitchChange = 1.0;
	bRealTimeMode = false;
	bEnableDebug = false;
	iSelectRoleIndex = 0;

	// load last state
	std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

	if (xmlState.get() != nullptr)
		if (xmlState->hasTagName("Config")) {
			fMaxSliceLength = (float)xmlState->getDoubleAttribute("fMaxSliceLength", 1.0);
			fMaxSliceLengthForRealTimeMode = (float)xmlState->getDoubleAttribute("fMaxSliceLengthForRealTimeMode", 1.0);
			fMaxSliceLengthForSentenceMode = (float)xmlState->getDoubleAttribute("fMaxSliceLengthForSentenceMode", 1.0);
			fLowVolumeDetectTime = (float)xmlState->getDoubleAttribute("fLowVolumeDetectTime", 0.4);
			lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
			fPitchChange = (float)xmlState->getDoubleAttribute("fPitchChange", 1.0);
			bRealTimeMode = (bool)xmlState->getBoolAttribute("bRealTimeMode", false);
			bEnableDebug = (bool)xmlState->getBoolAttribute("bEnableDebug", false);
			iSelectRoleIndex = (int)xmlState->getIntAttribute("iSelectRoleIndex", 0);
		}
}

void NetProcessJUCEVersionAudioProcessor::runWorker()
{
    // ����Worker�߳�
    std::thread(func_do_voice_transfer_worker,
        iNumberOfChanel,					// ͨ������
        getSampleRate(),		            // ��Ŀ������

        lModelInputOutputBufferSize,		// ģ�����������������С
        fModeulInputSampleBuffer,			// ģ�����뻺����
        &lModelInputSampleBufferReadPos,	// ģ�����뻺������ָ��
        &lModelInputSampleBufferWritePos,	// ģ�����뻺����дָ��

        fModeulOutputSampleBuffer,			// ģ�����������
        &lModelOutputSampleBufferReadPos,	// ģ�������������ָ��
        &lModelOutputSampleBufferWritePos,	// ģ�����������дָ��

        &fPrefixLength,						// ǰ��������ʱ��(s)
        &fDropSuffixLength,					// ������β��ʱ��(s)
        &fPitchChange,						// �����仯��ֵ
        &bCalcPitchError,					// �������������

        roleList,							// ���õĿ�����ɫ�б�
        &iSelectRoleIndex,					// ѡ��Ľ�ɫID
        dllFuncSrcSimple,					// DLL�ڲ�SrcSimple����

        &bEnableSOVITSPreResample,			// ����SOVITSģ�������Ƶ�ز���Ԥ����
        iSOVITSModelInputSamplerate,		// SOVITSģ����β�����
        &bEnableHUBERTPreResample,			// ����HUBERTģ�������Ƶ�ز���Ԥ����
        iHUBERTInputSampleRate,				// HUBERTģ����β�����

        &bRealTimeMode,					    // ռλ����ʵʱģʽ
        &bDoItSignal,						// ռλ������ʾ��worker�д����������
		&bEnableDebug,

        &bWorkerNeedExit					// ռλ������ʾworker�߳���Ҫ�˳�
        //&mWorkerSafeExit					// ����������ʾworker�߳��Ѿ���ȫ�˳�
    ).detach();


}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NetProcessJUCEVersionAudioProcessor();
}
