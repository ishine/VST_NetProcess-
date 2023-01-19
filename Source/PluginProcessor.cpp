/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <Windows.h>
#include <fstream>
#include "AudioWork.h"
using namespace std::chrono;

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
	initThis();
	runWorker();
}

void NetProcessJUCEVersionAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
	bWorkerNeedExit = true;
	// �����̻߳�������ʱ������������ϵģ���ʱ���̻߳������˳�
	// ���߳�ͨ�����˳��źŷ������̣߳��ȴ����̰߳�ȫ�˳����ͷ��������߳����˳�
	mWorkerSafeExit.lock();
	mWorkerSafeExit.unlock();
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
		}

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
				for (int i = 0; i < audioBuffer.getNumSamples(); i++) {
					fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = inputOutputL[i];
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
			if (inputBufferSize > lMaxSliceLengthSampleNumber) {
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
	if (!initDone) {
		// default value
		fMaxSliceLength = 5.0f;
		fMaxSliceLengthForRealTimeMode = fMaxSliceLength;
		fMaxSliceLengthForSentenceMode = fMaxSliceLength;
		fLowVolumeDetectTime = 0.4f;
		lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
		fPitchChange = 0.0f;
		bRealTimeMode = false;
		bEnableDebug = false;
		iSelectRoleIndex = 0;
	}

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

void NetProcessJUCEVersionAudioProcessor::initThis()
{
	std::wstring sDllPath = L"C:/Program Files/Common Files/VST3/NetProcessJUCEVersion/samplerate.dll";
	std::string sJsonConfigFileName = "C:/Program Files/Common Files/VST3/NetProcessJUCEVersion/netProcessConfig.json";
	auto dllClient = LoadLibraryW(sDllPath.c_str());
	if (dllClient != NULL) {
		dllFuncSrcSimple = (FUNC_SRC_SIMPLE)GetProcAddress(dllClient, "src_simple");
	}
	else {
		OutputDebugStringA("samplerate.dll load Error!");
	}

	// ��ȡJSON�����ļ�
	std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
	std::stringstream buffer_pc_file;
	buffer_pc_file << t_pc_file.rdbuf();

	juce::var jsonVar;
	if (juce::JSON::parse(buffer_pc_file.str(), jsonVar).wasOk()) {
		auto& props = jsonVar.getDynamicObject()->getProperties();
		bEnableSOVITSPreResample = props["bEnableSOVITSPreResample"];
		iSOVITSModelInputSamplerate = props["iSOVITSModelInputSamplerate"];
		bEnableHUBERTPreResample = props["bEnableHUBERTPreResample"];
		iHUBERTInputSampleRate = props["iHUBERTInputSampleRate"];
		fSampleVolumeWorkActiveVal = props["fSampleVolumeWorkActiveVal"];

		roleList.clear();
		auto jsonRoleList = props["roleList"];
		int iRoleSize = jsonRoleList.size();
		for (int i = 0; i < iRoleSize; i++) {
			auto& roleListI = jsonRoleList[i].getDynamicObject()->getProperties();
			std::string apiUrl = roleListI["apiUrl"].toString().toStdString();
			std::string name = roleListI["name"].toString().toStdString();
			std::string speakId = roleListI["speakId"].toString().toStdString();
			roleStruct role;
			role.sSpeakId = speakId;
			role.sName = name;
			role.sApiUrl = apiUrl;
			roleList.push_back(role);
		}
		if (iSelectRoleIndex + 1 > iRoleSize || iSelectRoleIndex < 0) {
			iSelectRoleIndex = 0;
		}
	}
	else {
		// error read json
	};

	iNumberOfChanel = 1;
	lNoOutputCount = 0;
	bDoItSignal = false;

	// ��ʼ���̼߳佻�����ݵĻ�������120s�Ļ������㹻��
	float fModelInputOutputBufferSecond = 120.f;
	lModelInputOutputBufferSize = static_cast<long>(fModelInputOutputBufferSecond * getSampleRate());
	fModeulInputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	fModeulOutputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	lModelInputSampleBufferReadPos = 0;
	lModelInputSampleBufferWritePos = 0;
	lModelOutputSampleBufferReadPos = 0;
	lModelOutputSampleBufferWritePos = 0;

	// worker�̰߳�ȫ�˳�����ź�
	bWorkerNeedExit = false;

	initDone = true;
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
		vServerUseTime,

        &bWorkerNeedExit,					// ռλ������ʾworker�߳���Ҫ�˳�
        &mWorkerSafeExit					// ����������ʾworker�߳��Ѿ���ȫ�˳�
    ).detach();


}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NetProcessJUCEVersionAudioProcessor();
}
