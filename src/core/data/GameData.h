#ifndef GAMEDATA_H
#define GAMEDATA_H

#include "core/data/GameDataV4.h"

using gamedata::AttributeDecl;
using gamedata::BoolOrExpr;
using gamedata::CompiledRecipePort;
using gamedata::CompiledRecipeProfile;
using gamedata::DiagnosticSeverity;
using gamedata::EvalMessage;
using gamedata::EvalStatus;
using gamedata::EvaluatedMetricCoeff;
using gamedata::EvaluatedNodeMetric;
using gamedata::EvaluatedPort;
using gamedata::ExtraPortDef;
using gamedata::GameMetadata;
using gamedata::GamePackage;
using gamedata::LoadDiagnostic;
using gamedata::LoadReport;
using gamedata::Machine;
using gamedata::MetricAggregate;
using gamedata::MetricDef;
using gamedata::MetricDisplayLocation;
using gamedata::MetricFormKind;
using gamedata::MetricTotal;
using gamedata::NodeEval;
using gamedata::NodeMetricsResult;
using gamedata::OptionDef;
using gamedata::OptionSet;
using gamedata::ParameterDef;
using gamedata::ParameterType;
using gamedata::ParameterUiDef;
using gamedata::ParamValue;
using gamedata::Profile;
using gamedata::ProfileMetricEntry;
using gamedata::RateUnit;
using gamedata::Recipe;
using gamedata::RecipeOverrides;
using gamedata::RecipePort;
using gamedata::RecipeTimeUnit;
using gamedata::RequireRule;
using gamedata::ResolvedGlobals;
using gamedata::Resource;
using gamedata::ResourceKindDef;
using gamedata::ScalarOrExpr;
using gamedata::SetupItem;
using gamedata::TableDef;
using gamedata::UiPlacement;
using gamedata::UiWidget;
using gamedata::UnitsDef;

using GameData = gamedata::GamePackage;

#endif // GAMEDATA_H
