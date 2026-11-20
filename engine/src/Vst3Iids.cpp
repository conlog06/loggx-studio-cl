// IIDs der verwendeten VST3-Schnittstellen (einmal im Programm definieren)
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

namespace Steinberg {
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IConnectionPoint)
DEF_CLASS_IID(IMessage)
DEF_CLASS_IID(IAttributeList)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
DEF_CLASS_IID(IEventList)
}  // namespace Vst
}  // namespace Steinberg
