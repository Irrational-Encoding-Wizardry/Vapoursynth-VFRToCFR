#include "VapourSynth.h"
#include "VSHelper.h"
#include <vector>
#include <deque>
#include <fstream>
#include <string>
#define EPSILON 0.0005

struct timestamp {
	unsigned int num;
	double start;
	double end;
};

struct VFRToCFRData {
	VSNodeRef *node;
	VSVideoInfo vi;
	bool drop;
	std::vector<unsigned int> frameMap;
};

static void VS_CC VFRToCFRInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	VFRToCFRData *d = static_cast<VFRToCFRData *>(*instanceData);
	vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC VFRToCFRGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
	VFRToCFRData *d = static_cast<VFRToCFRData *>(* instanceData);

	if (activationReason == arInitial) {
		vsapi->requestFrameFilter(d->frameMap[n], d->node, frameCtx);
	}
	else if (activationReason == arAllFramesReady) {
		const VSFrameRef *src = vsapi->getFrameFilter(d->frameMap[n], d->node, frameCtx);
		VSFrameRef *dst = vsapi->copyFrame(src, core);
		VSMap *m = vsapi->getFramePropsRW(dst);
		vsapi->freeFrame(src);
		vsapi->propSetInt(m, "_DurationNum", d->vi.fpsDen, paReplace);
		vsapi->propSetInt(m, "_DurationDen", d->vi.fpsNum, paReplace);
		return dst;
	}

	return nullptr;
}

static void VS_CC VFRToCFRFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	VFRToCFRData *d = static_cast<VFRToCFRData *>(instanceData);
	vsapi->freeNode(d->node);
	delete d;
}

static void VS_CC VFRToCFRCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	VFRToCFRData d{ vsapi->propGetNode(in, "clip", 0, 0), *vsapi->getVideoInfo(d.node) };
	int err;

	d.vi.fpsNum = vsapi->propGetInt(in, "fpsnum", 0, &err);
	d.vi.fpsDen = vsapi->propGetInt(in, "fpsden", 0, &err);
	if (d.vi.fpsNum <= 0 || d.vi.fpsDen <= 0) {
		vsapi->setError(out, "Both fpsnum and fpsden need to be greater than 0.");
		vsapi->freeNode(d.node);
		return;
	}

	d.drop = !!vsapi->propGetInt(in, "drop", 0, &err);
	if (err)
		d.drop = true;

	std::string timecodes{ vsapi->propGetData(in, "timecodes", 0, &err) };

	unsigned int count{ 0 };
	std::deque<timestamp> inTimes;
	std::ifstream file(timecodes);
	if (!file) {
		vsapi->setError(out, "Failed to open the timecodes file.");
		vsapi->freeNode(d.node);
		return;
	}
	std::string line;
	std::getline(file, line);
	while (file) {
		double i = 0;
		file >> i;
		timestamp time;
		time.start = i / 1000.0;
		time.num = count;
		count++;
		inTimes.push_back(time);
	}

	//If last line is NULL, remove the last timestamp.
	if (inTimes.size() > 1 && inTimes.back().start <= 0)
		inTimes.pop_back();

	for (unsigned int i = 0; i < inTimes.size(); i++) {
		if (i != inTimes.size() - 1) {
			inTimes[i].end = inTimes[i + 1].start;
		}
	}
	inTimes.back().end = inTimes.back().start + (inTimes.back().start / (inTimes.size() - 1));
	
	unsigned int frames = (inTimes.back().start * d.vi.fpsNum / d.vi.fpsDen);

	double check = frames / static_cast<double>(d.vi.fpsNum) * d.vi.fpsDen;
	while (check < inTimes.back().end && abs(check - inTimes.back().end) > static_cast<double>(EPSILON)) {
		frames++;
		check = frames / static_cast<double>(d.vi.fpsNum) * d.vi.fpsDen;
	}

	d.frameMap.resize(frames);
	bool drop = false;
	std::deque<timestamp> choices;
	for (unsigned int i = 0; i < frames; i++) {
		double starttime = i * d.vi.fpsDen / static_cast<double>(d.vi.fpsNum);
		double endtime = (i + 1) * d.vi.fpsDen / static_cast<double>(d.vi.fpsNum);

		//Remove frames that are eariler than the last frame chosen.
		while (!choices.empty() && d.frameMap[i - 1] > choices.front().num) {
			choices.pop_front();
		}

		//Remove frames than can't be a choice for this frame.
		while (!choices.empty() && (starttime > choices.front().end || abs(starttime - choices.front().end) < static_cast<double>(EPSILON))) {
			choices.pop_front();
		}

		//Add possible choices.
		while (!inTimes.empty() && endtime > inTimes.front().start && abs(endtime - inTimes.front().start) > static_cast<double>(EPSILON)) {
			
			//This if statement can only be false when the framerate from the timecodes
			//file is really low (unrealistic).
			if (starttime < inTimes.front().end && abs(starttime - inTimes.front().end) > static_cast<double>(EPSILON)) {
				choices.push_back(inTimes.front());
			}			
			inTimes.pop_front();
		}

		//Should only occur with timestamps not starting at 0.
		if (choices.size() == 0) {
			if (inTimes.front().num == 0) {
				d.frameMap[i] = 0;
			}
			else {
				vsapi->setError(out, "No valid matches found. Framerate might be too high.");
				vsapi->freeNode(d.node);
				return;
			}
		}
		else {
			std::deque<timestamp> choicesTemp{ choices };

			//Choose the VFR frame(s) with the longest duration.
			if (choicesTemp.size() > 1) {
				double longest = 0;
				for (unsigned int j = 0; j < choicesTemp.size(); j++) {
					double start;
					if (starttime > choicesTemp[j].start) {
						start = starttime;
					}
					else {
						start = choicesTemp[j].start;
					}
					double end;
					if (endtime < choicesTemp[j].end) {
						end = endtime;
					}
					else {
						end = choicesTemp[j].end;
					}
					if (longest < (end - start)) {
						longest = end - start;
					}
				}
				for (unsigned int j = 0; j < choicesTemp.size(); j++) {
					double start;
					if (starttime > choicesTemp[j].start) {
						start = starttime;
					}
					else {
						start = choicesTemp[j].start;
					}
					double end;
					if (endtime < choicesTemp[j].end) {
						end = endtime;
					}
					else {
						end = choicesTemp[j].end;
					}
					if (abs(longest - (end - start)) > static_cast<double>(EPSILON)) {
						choicesTemp.erase(choicesTemp.begin() + j);
						j--;
					}
				}
			}

			//Choose the VFR frame(s) that is closest to the center of the CFR frame.
			if (choicesTemp.size() > 1) {
				double mid = ((endtime - starttime) / 2) + starttime;
				double shortest = endtime - starttime;
				bool found = false;
				timestamp hasMid;
				for (unsigned int j = 0; j < choicesTemp.size(); j++) {
					if (abs(mid - choicesTemp[j].start) < shortest) {
						shortest = abs(mid - choicesTemp[j].start);
					}
					if (abs(mid - choicesTemp[j].end) < shortest) {
						shortest = abs(mid - choicesTemp[j].end);
					}
					if (mid > choicesTemp[j].start && abs(mid - choicesTemp[j].start) > static_cast<double>(EPSILON) && mid < choicesTemp[j].end
						&& abs(mid - choicesTemp[j].end) > static_cast<double>(EPSILON)) {
						hasMid = choicesTemp[j];
						found = true;
						break;
					}
				}
				if (found) {
					choicesTemp.clear();
					choicesTemp.push_back(hasMid);
				}
				else {
					for (unsigned int j = 0; j < choicesTemp.size(); j++) {
						if (abs(shortest - abs(mid - choicesTemp[j].start)) > static_cast<double>(EPSILON)
							&& abs(shortest - abs(mid - choicesTemp[j].end)) > static_cast<double>(EPSILON)) {
							choicesTemp.erase(choicesTemp.begin() + j);
							j--;
						}
					}
				}
			}

			//By now, there should be no more than two matches left.

			//Choose the frame that hasn't been used for the CFR video yet.
			if (choicesTemp.size() > 1 && i != 0) {
				if (d.frameMap[i - 1] == choicesTemp.front().num) {
					choicesTemp.pop_front();
				}
				else if (d.frameMap[i - 1] == choicesTemp.back().num) {
					choicesTemp.pop_back();
				}
			}

			//If none of them have been used, choose the first one.
			d.frameMap[i] = choicesTemp.front().num;

			//Check if frames were dropped.
			if (i != 0 && choicesTemp.front().num - d.frameMap[i - 1] > 1) {
				drop = true;
			}
		}
	}

	if (!d.drop && drop) {
		vsapi->setError(out, "Frames were dropped. Use drop = true if you want to allow frame drops.");
		vsapi->freeNode(d.node);
		return;
	}

	d.vi.numFrames = frames;

	VFRToCFRData* data = new VFRToCFRData{ d };

	vsapi->createFilter(in, out, "Invert", VFRToCFRInit, VFRToCFRGetFrame, VFRToCFRFree, fmParallel, 0, data, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	configFunc("blaze.plugin.vfrtocfr", "vfrtocfr", "VFR to CFR Video Converter", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("VFRToCFR", "clip:clip;timecodes:data;fpsnum:int;fpsden:int;drop:int:opt;", VFRToCFRCreate, nullptr, plugin);
}
