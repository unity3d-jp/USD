//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/usd/usdUtils/pipeline.h"

#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/primSpec.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/stringUtils.h"

#include <string>


TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (zUp)

    (UsdUtilsPipeline)
        (RegisteredVariantSets)
            (selectionExportPolicy)
                // lowerCamelCase of the enums.
                (never)
                (ifAuthored)
                (always)
);


bool UsdUtilsGetCamerasAreZup(UsdStageWeakPtr const &stage)
{
    if (not stage){
        return false;
    }
    
    SdfLayerHandle const &rootLayer = stage->GetRootLayer();
    
    bool hasZupCamera = false;

    TF_FOR_ALL(prim, stage->GetPseudoRoot().
                            GetFilteredChildren(UsdPrimIsDefined and
                                                not UsdPrimIsAbstract)){
        VtValue isZup = prim->GetCustomDataByKey(_tokens->zUp);
        if (isZup.IsEmpty()){
            continue;
        }
        else if (isZup.IsHolding<bool>()){
            if (isZup.Get<bool>()) {
                hasZupCamera = true;
            } else {
                // If any prim is y-Up, that trumps everything.
                return false;
            }
        }
        else {
            TF_WARN("Found non-boolean 'zUp' customData in UsdStage "
                    "root at layer '%s'."
                    "for isZup.", rootLayer->GetIdentifier().c_str());
        }
    }

    // If there's no customData, it will be Y-up.
    return hasZupCamera;
}

TfToken UsdUtilsGetAlphaAttributeNameForColor(TfToken const &colorAttrName)
{
    return TfToken(colorAttrName.GetString()+std::string("_A"));
}

TfToken
UsdUtilsGetModelNameFromRootLayer(
    const SdfLayerHandle& rootLayer)
{
    // First check if if we have the metadata.
    TfToken modelName = rootLayer->GetDefaultPrim();
    if (not modelName.IsEmpty()) {
        return modelName;
    }

    // If no default prim, see if there is a prim w/ the same "name" as the
    // file.  "name" here means the string before the first ".".
    const std::string& filePath = rootLayer->GetRealPath();
    std::string baseName = TfGetBaseName(filePath);
    modelName = TfToken(baseName.substr(0, baseName.find('.')));

    if (not modelName.IsEmpty() and rootLayer->GetPrimAtPath(
            SdfPath::AbsoluteRootPath().AppendChild(modelName))) {
        return modelName;
    }

    // Otherwise, fallback to getting the first non-class child in the layer.
    TF_FOR_ALL(rootChildrenIter, rootLayer->GetRootPrims()) {
        const SdfPrimSpecHandle& rootPrim = *rootChildrenIter;
        if (rootPrim->GetSpecifier() != SdfSpecifierClass) {
            return rootPrim->GetNameToken();
        }
    }

    return modelName;
}

TF_MAKE_STATIC_DATA(std::set<UsdUtilsRegisteredVariantSet>, _regVarSets)
{
    PlugPluginPtrVector plugs = PlugRegistry::GetInstance().GetAllPlugins();
    TF_FOR_ALL(plugIter, plugs) {
        PlugPluginPtr plug = *plugIter;
        JsObject metadata = plug->GetMetadata();
        JsValue pipelineUtilsDictValue;
        if (TfMapLookup(metadata, _tokens->UsdUtilsPipeline, &pipelineUtilsDictValue)) {
            if (not pipelineUtilsDictValue.Is<JsObject>()) {
                TF_CODING_ERROR(
                        "%s[UsdUtilsPipeline] was not a dictionary.",
                        plug->GetName().c_str());
                continue;
            }

            JsObject pipelineUtilsDict =
                pipelineUtilsDictValue.Get<JsObject>();

            JsValue registeredVariantSetsValue;
            if (TfMapLookup(pipelineUtilsDict,
                        _tokens->RegisteredVariantSets,
                        &registeredVariantSetsValue)) {
                if (not registeredVariantSetsValue.IsObject()) {
                    TF_CODING_ERROR(
                            "%s[UsdUtilsPipeline][RegisteredVariantSets] was not a dictionary.",
                            plug->GetName().c_str());
                    continue;
                }

                JsObject registeredVariantSets = registeredVariantSetsValue.GetObject();
                for (const auto& i: registeredVariantSets) {
                    const std::string& variantSetName = i.first;
                    const JsValue& v = i.second;
                    if (not v.IsObject()) {
                        TF_CODING_ERROR(
                                "%s[UsdUtilsPipeline][RegisteredVariantSets][%s] was not a dictionary.",
                                plug->GetName().c_str(),
                                variantSetName.c_str());
                        continue;
                    }

                    JsObject info = v.GetObject();
                    std::string variantSetType = info[_tokens->selectionExportPolicy].GetString();


                    UsdUtilsRegisteredVariantSet::SelectionExportPolicy selectionExportPolicy;
                    if (variantSetType == _tokens->never) {
                        selectionExportPolicy = 
                            UsdUtilsRegisteredVariantSet::SelectionExportPolicy::Never;
                    }
                    else if (variantSetType == _tokens->ifAuthored) {
                        selectionExportPolicy = 
                            UsdUtilsRegisteredVariantSet::SelectionExportPolicy::IfAuthored;
                    }
                    else if (variantSetType == _tokens->always) {
                        selectionExportPolicy = 
                            UsdUtilsRegisteredVariantSet::SelectionExportPolicy::Always;
                    }
                    else {
                        TF_CODING_ERROR(
                                "%s[UsdUtilsPipeline][RegisteredVariantSets][%s] was not valid.",
                                plug->GetName().c_str(),
                                variantSetName.c_str());
                        continue;
                    }
                    _regVarSets->insert(UsdUtilsRegisteredVariantSet(
                                variantSetName, selectionExportPolicy));
                }
            }
        }
    }
}

const std::set<UsdUtilsRegisteredVariantSet>&
UsdUtilsGetRegisteredVariantSets()
{
    return *_regVarSets;
}

UsdPrim 
UsdUtilsGetPrimAtPathWithForwarding(const UsdStagePtr &stage, 
                                    const SdfPath &path)
{
   if (UsdPrim p = stage->GetPrimAtPath(path))
        return p;

    SdfPath validAncestorPath = path;
    UsdPrim validAncestor;
    while (not validAncestor) {
        if (validAncestorPath == SdfPath::AbsoluteRootPath() or 
            validAncestorPath == SdfPath::EmptyPath()) {
            break;
        }

        validAncestorPath = validAncestorPath.GetParentPath();
        validAncestor = stage->GetPrimAtPath(validAncestorPath);
    }

    if (validAncestorPath.IsPrimPath()) {
        if (not validAncestor.IsInstance())
            return UsdPrim();

        SdfPath instanceRelPath = path.ReplacePrefix(validAncestorPath, 
            SdfPath::ReflexiveRelativePath());
        UsdPrim master = validAncestor.GetMaster();
        if (TF_VERIFY(master)) {
            SdfPath masterPath = master.GetPath().AppendPath(instanceRelPath);

            return UsdUtilsGetPrimAtPathWithForwarding(stage, masterPath);
        }
    }

    return UsdPrim();
}

UsdPrim 
UsdUtilsUninstancePrimAtPath(const UsdStagePtr &stage, 
                             const SdfPath &path)
{
    // If a valid prim exists at the requested path, simply return it.
    if (UsdPrim p = stage->GetPrimAtPath(path))
        return p;

    // Check if the path can be forwarded to a valid prim in a master.
    if (not UsdUtilsGetPrimAtPathWithForwarding(stage, path))
        return UsdPrim();

    SdfPath validAncestorPath = path;
    UsdPrim validAncestor;
    while (not validAncestor) {
        if (validAncestorPath == SdfPath::AbsoluteRootPath() or 
            validAncestorPath == SdfPath::EmptyPath()) {
            break;
        }

        validAncestorPath = validAncestorPath.GetParentPath();
        validAncestor = stage->GetPrimAtPath(validAncestorPath);
    }

    if (validAncestorPath.IsPrimPath()) {
        if (not TF_VERIFY(validAncestor.IsInstance()))
            return UsdPrim();

        validAncestor.SetInstanceable(false);
        return UsdUtilsUninstancePrimAtPath(stage, path);
    }

    return UsdPrim(); 
}

TfToken UsdUtilsGetPrimaryUVSetName()
{
    return TfToken("st");
}
