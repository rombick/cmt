#include "ikRigNode.h"

#include <maya/MAngle.h>
#include <maya/MEulerRotation.h>
#include <maya/MFloatVector.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MMatrix.h>
#include <maya/MQuaternion.h>
#include <maya/MTransformationMatrix.h>

#include <array>

#include "common.h"

MTypeId IKRigNode::id(0x0011581B);
MObject IKRigNode::aOutTranslate;
MObject IKRigNode::aOutRotate;
MObject IKRigNode::aOutRotateX;
MObject IKRigNode::aOutRotateY;
MObject IKRigNode::aOutRotateZ;
MObject IKRigNode::aOutRootMotion;
MObject IKRigNode::aInMatrix;
MObject IKRigNode::aInRestMatrix;
MObject IKRigNode::aTargetRestMatrix;
MObject IKRigNode::aLeftLegTwistOffset;
MObject IKRigNode::aRightLegTwistOffset;
MObject IKRigNode::aStrideScale;
MObject IKRigNode::aRootMotionScale;
MObject IKRigNode::aCharacterScale;
MObject IKRigNode::aLeftHandOffset;

const MString IKRigNode::kName("ikRig");

#define MATRIX_INPUT(obj, name)          \
  {                                      \
    obj = mAttr.create(name, name);      \
    addAttribute(obj);                   \
    mAttr.setArray(true);                \
    mAttr.setUsesArrayDataBuilder(true); \
    affects(obj);                        \
  }

MStatus IKRigNode::initialize() {
  MStatus status;

  MFnEnumAttribute eAttr;
  MFnMatrixAttribute mAttr;
  MFnNumericAttribute nAttr;
  MFnUnitAttribute uAttr;

  aOutTranslate = nAttr.createPoint("outputTranslate", "outputTranslate");
  nAttr.setArray(true);
  nAttr.setUsesArrayDataBuilder(true);
  nAttr.setWritable(false);
  nAttr.setStorable(false);
  addAttribute(aOutTranslate);

  aOutRotateX = uAttr.create("outputRotateX", "outputRotateX", MFnUnitAttribute::kAngle);
  aOutRotateY = uAttr.create("outputRotateY", "outputRotateY", MFnUnitAttribute::kAngle);
  aOutRotateZ = uAttr.create("outputRotateZ", "outputRotateZ", MFnUnitAttribute::kAngle);
  aOutRotate = nAttr.create("outputRotate", "outputRotate", aOutRotateX, aOutRotateY, aOutRotateZ);
  nAttr.setArray(true);
  nAttr.setUsesArrayDataBuilder(true);
  nAttr.setWritable(false);
  nAttr.setStorable(false);
  addAttribute(aOutRotate);

  aOutRootMotion = mAttr.create("rootMotion", "rootMotion");
  mAttr.setWritable(false);
  mAttr.setStorable(false);
  addAttribute(aOutRootMotion);

  aLeftLegTwistOffset =
      nAttr.create("leftLegTwistOffset", "leftLegTwistOffset", MFnNumericData::kFloat, 0.0);
  nAttr.setKeyable(true);
  addAttribute(aLeftLegTwistOffset);
  affects(aLeftLegTwistOffset);

  aRightLegTwistOffset =
      nAttr.create("rightLegTwistOffset", "rightLegTwistOffset", MFnNumericData::kFloat, 0.0);
  nAttr.setKeyable(true);
  addAttribute(aRightLegTwistOffset);
  affects(aRightLegTwistOffset);

  aStrideScale = nAttr.create("strideScale", "strideScale", MFnNumericData::kFloat, 1.0);
  nAttr.setKeyable(true);
  nAttr.setMin(0.0);
  addAttribute(aStrideScale);
  affects(aStrideScale);

  aRootMotionScale =
      nAttr.create("rootMotionScale", "rootMotionScale", MFnNumericData::kFloat, 1.0);
  nAttr.setKeyable(true);
  nAttr.setMin(0.0);
  addAttribute(aRootMotionScale);
  affects(aRootMotionScale);

  aCharacterScale = nAttr.create("characterScale", "characterScale", MFnNumericData::kFloat, 1.0);
  nAttr.setKeyable(true);
  nAttr.setMin(0.0);
  addAttribute(aCharacterScale);
  affects(aCharacterScale);

  aLeftHandOffset = mAttr.create("leftHandOffset", "leftHandOffset");
  addAttribute(aLeftHandOffset);
  affects(aLeftHandOffset);

  MATRIX_INPUT(aInMatrix, "inMatrix");
  MATRIX_INPUT(aInRestMatrix, "inRestMatrix");
  MATRIX_INPUT(aTargetRestMatrix, "targetRestMatrix");

  return MS::kSuccess;
}

void IKRigNode::affects(const MObject& attribute) {
  attributeAffects(attribute, aOutTranslate);
  attributeAffects(attribute, aOutRotate);
  attributeAffects(attribute, aOutRotateX);
  attributeAffects(attribute, aOutRotateY);
  attributeAffects(attribute, aOutRotateZ);
  attributeAffects(attribute, aOutRootMotion);
}

void* IKRigNode::creator() { return new IKRigNode(); }

IKRigNode::IKRigNode() : strideScale_(1.0), spineScale_(1.0), hipScale_(1.0) {
  inputMatrix_.setLength(IKRig_Count);
  inputRestMatrix_.setLength(IKRig_Count);
  targetRestMatrix_.setLength(IKRig_Count);
  rotationDelta_.resize(IKRig_Count);
  translationDelta_.setLength(IKRig_Count);
  prevForward_.push(MVector::zAxis);
  prevForward_.push(MVector::zAxis);
}

IKRigNode::~IKRigNode() {}

MStatus IKRigNode::compute(const MPlug& plug, MDataBlock& data) {
  MStatus status;

  if (plug != aOutTranslate && plug != aOutRotate && plug != aOutRootMotion) {
    return MS::kUnknownParameter;
  }

  // Get the input skeleton
  MArrayDataHandle hInputMatrices = data.inputArrayValue(aInMatrix);
  MArrayDataHandle hInputRestMatrices = data.inputArrayValue(aInRestMatrix);
  MArrayDataHandle hOutputRestMatrices = data.inputArrayValue(aTargetRestMatrix);
  for (unsigned int i = 0; i < IKRig_Count; ++i) {
    status = JumpToElement(hInputMatrices, i);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    inputMatrix_[i] = hInputMatrices.inputValue().asMatrix();

    status = JumpToElement(hInputRestMatrices, i);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    inputRestMatrix_[i] = hInputRestMatrices.inputValue().asMatrix();

    status = JumpToElement(hOutputRestMatrices, i);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    targetRestMatrix_[i] = hOutputRestMatrices.inputValue().asMatrix();
  }

  rootMotionScale_ = data.inputValue(aRootMotionScale).asFloat();
  strideScale_ = data.inputValue(aStrideScale).asFloat();
  characterScale_ = data.inputValue(aCharacterScale).asFloat();
  leftHandOffset_ = data.inputValue(aLeftHandOffset).asMatrix();

  // Calculate outputs
  for (unsigned int i = 0; i < IKRig_Count; ++i) {
    MTransformationMatrix tMatRest(inputRestMatrix_[i]);
    MQuaternion rRest = tMatRest.rotation();
    MVector tRest = tMatRest.translation(MSpace::kWorld);

    MTransformationMatrix tMatCurrent(inputMatrix_[i]);
    MQuaternion rCurrent = tMatCurrent.rotation();
    MVector tCurrent = tMatCurrent.translation(MSpace::kWorld);

    rotationDelta_[i] = rRest.inverse() * rCurrent;
    translationDelta_[i] = tCurrent - tRest;
  }

  // Calculate Root Motion
  rootMotion_ = calculateRootMotion();
  scaledRootMotion_ = rootMotion_;
  scaledRootMotion_[3][0] *= rootMotionScale_;
  scaledRootMotion_[3][2] *= rootMotionScale_;
  toScaledRootMotion_ = rootMotion_.inverse() * scaledRootMotion_;
  MDataHandle hRootMotion = data.outputValue(aOutRootMotion);
  hRootMotion.setMMatrix(scaledRootMotion_);
  hRootMotion.setClean();

  // Set outputs
  MArrayDataHandle hOutputTranslate = data.outputArrayValue(aOutTranslate);
  MArrayDataHandle hOutputRotate = data.outputArrayValue(aOutRotate);

  // Hips
  hipScale_ = position(targetRestMatrix_[IKRig_Hips]).y / position(inputRestMatrix_[IKRig_Hips]).y;
  hips_ = inputMatrix_[IKRig_Hips] * rootMotion_.inverse();
  MVector restInputHips = position(inputRestMatrix_[IKRig_Hips]);
  MVector scaledHipPosition = restInputHips + (position(hips_) - restInputHips) * hipScale_;
  hips_[3][0] = scaledHipPosition.x;
  hips_[3][1] = scaledHipPosition.y;
  hips_[3][2] = scaledHipPosition.z;
  hips_ *= rootMotion_;
  MVector hipDelta = position(hips_) - restInputHips;
  hips_ = offsetMatrix(targetRestMatrix_[IKRig_Hips], rotationDelta_[IKRig_Hips], hipDelta);
  status = setOutput(hOutputTranslate, hOutputRotate, IKRig_Hips, hips_ * toScaledRootMotion_);
  CHECK_MSTATUS_AND_RETURN_IT(status);

  // Left leg
  float leftLegTwistOffset = data.inputValue(aLeftLegTwistOffset).asFloat();
  status = calculateLegIk(IKRig_LeftUpLeg, IKRig_LeftLoLeg, IKRig_LeftFoot, hips_,
                          leftLegTwistOffset, hOutputTranslate, hOutputRotate);
  CHECK_MSTATUS_AND_RETURN_IT(status);

  // Right leg
  float rightLegTwistOffset = data.inputValue(aRightLegTwistOffset).asFloat();
  status = calculateLegIk(IKRig_RightUpLeg, IKRig_RightLoLeg, IKRig_RightFoot, hips_,
                          rightLegTwistOffset, hOutputTranslate, hOutputRotate);
  CHECK_MSTATUS_AND_RETURN_IT(status);

  // Chest
  status = calculateChestIk(hOutputTranslate, hOutputRotate);
  CHECK_MSTATUS_AND_RETURN_IT(status);

  // Left arm
  status = calculateArmIk(IKRig_LeftClavicle, IKRig_LeftShoulder, IKRig_LeftElbow, IKRig_LeftHand,
                          chest_, 0.0f, leftHandOffset_, hOutputTranslate, hOutputRotate);
  CHECK_MSTATUS_AND_RETURN_IT(status);

  // Right arm
  status =
      calculateArmIk(IKRig_RightClavicle, IKRig_RightShoulder, IKRig_RightElbow, IKRig_RightHand,
                     chest_, 0.0f, MMatrix::identity, hOutputTranslate, hOutputRotate);
  CHECK_MSTATUS_AND_RETURN_IT(status);

  status = calculateHeadIk(chest_, hOutputTranslate, hOutputRotate);
  CHECK_MSTATUS_AND_RETURN_IT(status);

  hOutputTranslate.setAllClean();
  hOutputRotate.setAllClean();

  return MS::kSuccess;
}

MMatrix IKRigNode::calculateRootMotion() {
  std::array<int, 4> rootInfluenceIndex = {IKRig_Hips, IKRig_Chest, IKRig_LeftUpLeg,
                                           IKRig_RightUpLeg};
  double weights[] = {0.5, 0.3, 0.1, 0.1};
  MVector rootMotionTranslate;
  MVector restRootMotionTranslate;
  int col = 0;
  MVector forward;
  for (const auto& i : rootInfluenceIndex) {
    forward += MVector::zAxis.rotateBy(rotationDelta_[i]) * weights[col];

    rootMotionTranslate +=
        MTransformationMatrix(inputMatrix_[i]).translation(MSpace::kWorld) * weights[col];
    restRootMotionTranslate +=
        MTransformationMatrix(inputRestMatrix_[i]).translation(MSpace::kWorld) * weights[col];
    ++col;
  }
  forward.y = 0.0;
  forward.normalize();

  // Average with previous two forward vectors
  forward += prevForward_.front();
  prevForward_.pop();
  forward += prevForward_.front();
  forward.normalize();
  prevForward_.push(forward);

  MVector x = MVector::yAxis ^ forward;
  MMatrix m;
  m[0][0] = x.x;
  m[0][1] = x.y;
  m[0][2] = x.z;
  m[1][0] = 0.0;
  m[1][1] = 1.0;
  m[1][2] = 0.0;
  m[2][0] = forward.x;
  m[2][1] = forward.y;
  m[2][2] = forward.z;
  m[3][0] = rootMotionTranslate.x;
  m[3][1] = 0.0;
  m[3][2] = rootMotionTranslate.z;

  MMatrix restM;
  restM[3][0] = restRootMotionTranslate.x;
  restM[3][2] = restRootMotionTranslate.z;
  m *= restM.inverse();

  return m;
}

MStatus IKRigNode::calculateLegIk(unsigned int upLegIdx, unsigned int loLegIdx,
                                  unsigned int footIdx, const MMatrix& hips, float twist,
                                  MArrayDataHandle& hOutputTranslate,
                                  MArrayDataHandle& hOutputRotate) {
  MStatus status;

  MMatrix upLeg = targetRestMatrix_[upLegIdx] * targetRestMatrix_[IKRig_Hips].inverse() * hips;
  MMatrix loLeg = targetRestMatrix_[loLegIdx] * targetRestMatrix_[upLegIdx].inverse() * upLeg;
  MMatrix foot = targetRestMatrix_[footIdx] * targetRestMatrix_[loLegIdx].inverse() * loLeg;

  // Foot target
  // Account for differences in ankle height to help with ground contact
  float ankleHeightDelta =
      position(targetRestMatrix_[footIdx]).y - position(inputRestMatrix_[footIdx]).y;
  MMatrix footRest = targetRestMatrix_[footIdx];
  MMatrix flatFootBindMatrix;
  flatFootBindMatrix[3][0] = footRest[3][0];
  flatFootBindMatrix[3][2] = footRest[3][2];

  MMatrix footTarget = inputRestMatrix_[footIdx];
  footTarget[3][1] += ankleHeightDelta;
  MVector footTranslationDelta = translationDelta_[footIdx];
  footTranslationDelta.y *= hipScale_;
  footTarget = offsetMatrix(footTarget, rotationDelta_[footIdx], footTranslationDelta);
  footTarget *= rootMotion_.inverse() * flatFootBindMatrix.inverse();
  // Scale foot position relative to resting stance
  footTarget[3][0] *= strideScale_;
  footTarget[3][2] *= strideScale_;
  footTarget *= flatFootBindMatrix * rootMotion_;

  // Calculate leg ik
  MVector ia = position(inputRestMatrix_[upLegIdx]);
  MVector ib = position(inputRestMatrix_[loLegIdx]);
  MVector ic = position(inputRestMatrix_[footIdx]);
  MVector iac = (ic - ia).normal();
  MVector twistAxis = position(footTarget) - position(upLeg);
  // MVector pv = (ib - (ia + (iac * ((ib - ia) * iac)))).normal() * outputDelta_[upLegIdx];
  MVector pv = (ib - (ia + (iac * ((ib - ia) * iac)))).normal().rotateBy(rotationDelta_[upLegIdx]);
  // Apply any twist offset
  MQuaternion tw(twist * 0.0174533, twistAxis);
  pv = pv.rotateBy(tw);
  pv += position(upLeg);
  MMatrix ikUpLeg, ikLoLeg;
  calculateTwoBoneIk(upLeg, loLeg, foot, footTarget, pv, ikUpLeg, ikLoLeg);

  MQuaternion footRotOffset =
      MTransformationMatrix(targetRestMatrix_[footIdx] * inputRestMatrix_[footIdx].inverse())
          .rotation();
  MQuaternion footInputRot = MTransformationMatrix(inputMatrix_[footIdx]).rotation();
  footRotOffset *= footInputRot;
  MMatrix ikFootPos = targetRestMatrix_[footIdx] * targetRestMatrix_[loLegIdx].inverse() * ikLoLeg;
  MTransformationMatrix tIkFoot(ikFootPos);
  tIkFoot.setRotationQuaternion(footRotOffset.x, footRotOffset.y, footRotOffset.z, footRotOffset.w);
  MMatrix ikFoot = tIkFoot.asMatrix();

  ikUpLeg *= toScaledRootMotion_;
  ikLoLeg *= toScaledRootMotion_;
  ikFoot *= toScaledRootMotion_;

  status = setOutput(hOutputTranslate, hOutputRotate, upLegIdx, ikUpLeg);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  status = setOutput(hOutputTranslate, hOutputRotate, loLegIdx, ikLoLeg);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  status = setOutput(hOutputTranslate, hOutputRotate, footIdx, ikFoot);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  return MS::kSuccess;
}

MMatrix IKRigNode::offsetMatrix(const MMatrix& m, const MQuaternion& r, const MVector& t) {
  MTransformationMatrix tm(m);
  tm.rotateBy(r, MSpace::kPostTransform);
  tm.addTranslation(t, MSpace::kPostTransform);
  return tm.asMatrix();
}

/*

    @return The world space matrix of child with a scaled translation delta relative to the parent
    in root motion space
*/
MMatrix IKRigNode::scaleRelativeTo(unsigned int inputChildIdx, unsigned int inputParentIdx,
                                   double scale, const MMatrix& targetParent) {
  MMatrix restChild = inputRestMatrix_[inputChildIdx] * inputRestMatrix_[inputParentIdx].inverse() *
                      inputMatrix_[inputParentIdx];

  MTransformationMatrix tMatRest(restChild);
  MQuaternion rRest = tMatRest.rotation();
  MVector tRest = tMatRest.translation(MSpace::kWorld);

  MTransformationMatrix tMatCurrent(inputMatrix_[inputChildIdx]);
  MQuaternion rCurrent = tMatCurrent.rotation();
  MVector tCurrent = tMatCurrent.translation(MSpace::kWorld);

  MQuaternion rotationDelta = rRest.inverse() * rCurrent;
  MVector translationDelta = tCurrent - tRest;
  translationDelta *= scale;

  MMatrix restTarget =
      targetRestMatrix_[inputChildIdx] * targetRestMatrix_[inputParentIdx].inverse() * targetParent;
  restTarget = offsetMatrix(restTarget, rotationDelta, translationDelta);

  return restTarget;
}

void IKRigNode::calculateTwoBoneIk(const MMatrix& root, const MMatrix& mid, const MMatrix& effector,
                                   const MMatrix& target, const MVector& pv, MMatrix& ikA,
                                   MMatrix& ikB) {
  MVector a = position(root);
  MVector b = position(mid);
  MVector c = position(effector);
  MVector t = position(target);
  MQuaternion a_gr = MTransformationMatrix(root).rotation();
  MQuaternion b_gr = MTransformationMatrix(mid).rotation();
  MVector ac = (c - a).normal();
  MVector d = (b - (a + (ac * ((b - a) * ac)))).normal();

  twoBoneIk(a, b, c, d, t, pv, a_gr, b_gr);

  ikA = a_gr.asMatrix();
  ikA[3][0] = a.x;
  ikA[3][1] = a.y;
  ikA[3][2] = a.z;
  ikB = b_gr.asMatrix();
  MMatrix midPos = mid * root.inverse() * ikA;
  ikB[3][0] = midPos[3][0];
  ikB[3][1] = midPos[3][1];
  ikB[3][2] = midPos[3][2];
}

// http://theorangeduck.com/page/simple-two-joint
void IKRigNode::twoBoneIk(const MVector& a, const MVector& b, const MVector& c, const MVector& d,
                          const MVector& t, const MVector& pv, MQuaternion& a_gr,
                          MQuaternion& b_gr) {
  float eps = 0.001f;
  float lab = (b - a).length();
  float lcb = (b - c).length();
  float lat = clamp((t - a).length(), eps, lab + lcb - eps);

  // Get current interior angles of start and mid
  float ac_ab_0 = acos(clamp((c - a).normal() * (b - a).normal(), -1.0f, 1.0f));
  float ba_bc_0 = acos(clamp((a - b).normal() * (c - b).normal(), -1.0f, 1.0f));
  float ac_at_0 = acos(clamp((c - a).normal() * (t - a).normal(), -1.0f, 1.0f));

  // Get desired interior angles
  float ac_ab_1 =
      acos(clamp((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat), -1.0f, 1.0f));
  float ba_bc_1 =
      acos(clamp((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb), -1.0f, 1.0f));
  MVector axis0 = ((c - a) ^ d).normal();
  MVector axis1 = ((c - a) ^ (t - a)).normal();

  MQuaternion r0(ac_ab_1 - ac_ab_0, axis0);
  MQuaternion r1(ba_bc_1 - ba_bc_0, axis0);
  MQuaternion r2(ac_at_0, axis1);

  // Pole vector rotation
  // Determine the rotation used to rotate the normal of the triangle formed by
  // a.b.c post r0*r2 rotation to the normal of the triangle formed by triangle a.pv.t
  MVector n1 = ((c - a) ^ (b - a)).normal().rotateBy(r0).rotateBy(r2);
  MVector n2 = ((t - a) ^ (pv - a)).normal();
  MQuaternion r3 = n1.rotateTo(n2);

  a_gr *= r0 * r2 * r3;
  b_gr *= r1;
  // Since we are calculating in world space, apply the start rotations to the mid
  b_gr *= r0 * r2 * r3;
}

MStatus IKRigNode::calculateChestIk(MArrayDataHandle& hOutputTranslate,
                                    MArrayDataHandle& hOutputRotate) {
  MStatus status;
  float targetSpineLength =
      position(targetRestMatrix_[IKRig_Chest]).y - position(targetRestMatrix_[IKRig_Hips]).y;
  float inputSpineLength =
      position(inputRestMatrix_[IKRig_Chest]).y - position(inputRestMatrix_[IKRig_Hips]).y;
  // Scale the local xform translation delta of the of the chest based on the spine length ratio
  spineScale_ = targetSpineLength / inputSpineLength;
  chest_ = scaleRelativeTo(IKRig_Chest, IKRig_Hips, spineScale_, hips_);
  status = setOutput(hOutputTranslate, hOutputRotate, IKRig_Chest, chest_ * toScaledRootMotion_);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  return MS::kSuccess;
}

MStatus IKRigNode::calculateArmIk(unsigned int clavicleIdx, unsigned int upArmIdx,
                                  unsigned int loArmIdx, unsigned int handIdx, const MMatrix& chest,
                                  float twist, const MMatrix& offset,
                                  MArrayDataHandle& hOutputTranslate,
                                  MArrayDataHandle& hOutputRotate) {
  MStatus status;

  MQuaternion clavicleOffset =
      MTransformationMatrix(targetRestMatrix_[clavicleIdx]).rotation() *
      MTransformationMatrix(inputRestMatrix_[clavicleIdx].inverse()).rotation();
  MQuaternion clavicleRotation =
      clavicleOffset * MTransformationMatrix(inputMatrix_[clavicleIdx]).rotation();
  MPoint claviclePosition =
      MTransformationMatrix(targetRestMatrix_[clavicleIdx]).translation(MSpace::kWorld);
  claviclePosition *= targetRestMatrix_[IKRig_Chest].inverse() * chest;
  MMatrix clavicle = clavicleRotation.asMatrix();
  clavicle[3][0] = claviclePosition.x;
  clavicle[3][1] = claviclePosition.y;
  clavicle[3][2] = claviclePosition.z;

  MMatrix upArm = targetRestMatrix_[upArmIdx] * targetRestMatrix_[clavicleIdx].inverse() * clavicle;
  MMatrix loArm = targetRestMatrix_[loArmIdx] * targetRestMatrix_[upArmIdx].inverse() * upArm;
  MMatrix hand = targetRestMatrix_[handIdx] * targetRestMatrix_[loArmIdx].inverse() * loArm;

  // Hand target
  // Account for differences in arm length
  float targetArmLength =
      (position(targetRestMatrix_[loArmIdx]) - position(targetRestMatrix_[upArmIdx])).length() +
      (position(targetRestMatrix_[handIdx]) - position(targetRestMatrix_[loArmIdx])).length();
  float inArmLength =
      (position(inputRestMatrix_[loArmIdx]) - position(inputRestMatrix_[upArmIdx])).length() +
      (position(inputRestMatrix_[handIdx]) - position(inputRestMatrix_[loArmIdx])).length();

  float armScale = targetArmLength / inArmLength;
  MVector upArmPosition = position(upArm);
  MMatrix handTarget = scaleRelativeTo(handIdx, clavicleIdx, armScale, clavicle);

  // Calculate arm ik
  MVector ia = position(inputRestMatrix_[upArmIdx]);
  MVector ib = position(inputRestMatrix_[loArmIdx]);
  MVector ic = position(inputRestMatrix_[handIdx]);
  MVector iac = (ic - ia).normal();
  MVector twistAxis = position(handTarget) - position(upArm);
  // pv location is vector from input elbow projected on to shoulder-to-hand vector to elbow
  // rotated in to worldspace
  MVector pv = (ib - (ia + (iac * ((ib - ia) * iac)))).normal().rotateBy(rotationDelta_[upArmIdx]);
  // Apply any twist offset
  MQuaternion tw(twist * 0.0174533, twistAxis);
  pv = pv.rotateBy(tw);
  pv += position(upArm);
  MMatrix ikUpArm, ikLoArm;
  calculateTwoBoneIk(upArm, loArm, hand, handTarget, pv, ikUpArm, ikLoArm);

  // Hand rotation
  MQuaternion handOffset = MTransformationMatrix(targetRestMatrix_[handIdx]).rotation() *
                           MTransformationMatrix(inputRestMatrix_[handIdx].inverse()).rotation();
  MQuaternion handRotation = handOffset * MTransformationMatrix(inputMatrix_[handIdx]).rotation();
  MMatrix ikHandPos = targetRestMatrix_[handIdx] * targetRestMatrix_[loArmIdx].inverse() * ikLoArm;
  MTransformationMatrix tIkHand(ikHandPos);
  tIkHand.setRotationQuaternion(handRotation.x, handRotation.y, handRotation.z, handRotation.w);
  MMatrix ikHand = tIkHand.asMatrix();

  clavicle *= toScaledRootMotion_;
  ikUpArm *= toScaledRootMotion_;
  ikLoArm *= toScaledRootMotion_;
  ikHand *= toScaledRootMotion_;

  status = setOutput(hOutputTranslate, hOutputRotate, clavicleIdx, clavicle);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  status = setOutput(hOutputTranslate, hOutputRotate, upArmIdx, ikUpArm);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  status = setOutput(hOutputTranslate, hOutputRotate, loArmIdx, ikLoArm);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  status = setOutput(hOutputTranslate, hOutputRotate, handIdx, ikHand);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  return MS::kSuccess;
}

MStatus IKRigNode::calculateHeadIk(const MMatrix& chest, MArrayDataHandle& hOutputTranslate,
                                   MArrayDataHandle& hOutputRotate) {
  MStatus status;

  // Neck rotation
  MQuaternion neckOffset = MTransformationMatrix(targetRestMatrix_[IKRig_Neck]).rotation() *
                           MTransformationMatrix(inputRestMatrix_[IKRig_Neck].inverse()).rotation();
  MQuaternion neckRotation =
      neckOffset * MTransformationMatrix(inputMatrix_[IKRig_Neck]).rotation();
  MMatrix ikNeckPos =
      targetRestMatrix_[IKRig_Neck] * targetRestMatrix_[IKRig_Chest].inverse() * chest;
  MTransformationMatrix tIkNeck(ikNeckPos);
  tIkNeck.setRotationQuaternion(neckRotation.x, neckRotation.y, neckRotation.z, neckRotation.w);
  MMatrix neck = tIkNeck.asMatrix();
  status = setOutput(hOutputTranslate, hOutputRotate, IKRig_Neck, neck * toScaledRootMotion_);
  CHECK_MSTATUS_AND_RETURN_IT(status);

  float targetNeckLength =
      position(targetRestMatrix_[IKRig_Head]).y - position(targetRestMatrix_[IKRig_Neck]).y;
  float inputNeckLength =
      position(inputRestMatrix_[IKRig_Head]).y - position(inputRestMatrix_[IKRig_Neck]).y;
  // Scale the local xform translation delta of the of the chest based on the spine length ratio
  neckScale_ = targetNeckLength / inputNeckLength;
  MMatrix head = scaleRelativeTo(IKRig_Head, IKRig_Neck, neckScale_, neck);
  status = setOutput(hOutputTranslate, hOutputRotate, IKRig_Head, head * toScaledRootMotion_);
  CHECK_MSTATUS_AND_RETURN_IT(status);

  return MS::kSuccess;
}

MStatus IKRigNode::setOutput(MArrayDataHandle& hOutputTranslate, MArrayDataHandle& hOutputRotate,
                             unsigned int bodyPart, const MMatrix& matrix) {
  MStatus status;
  MFloatVector position(matrix[3][0], matrix[3][1], matrix[3][2]);
  status = JumpToElement(hOutputTranslate, bodyPart);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  MDataHandle hOutput = hOutputTranslate.outputValue();
  hOutput.setMFloatVector(position);
  hOutput.setClean();

  MEulerRotation r = MEulerRotation::decompose(matrix, MEulerRotation::kXYZ);
  MAngle rx(r.x);
  MAngle ry(r.y);
  MAngle rz(r.z);
  status = JumpToElement(hOutputRotate, bodyPart);
  CHECK_MSTATUS_AND_RETURN_IT(status);
  hOutput = hOutputRotate.outputValue();

  MDataHandle hX = hOutput.child(aOutRotateX);
  MDataHandle hY = hOutput.child(aOutRotateY);
  MDataHandle hZ = hOutput.child(aOutRotateZ);
  hX.setMAngle(rx);
  hY.setMAngle(ry);
  hZ.setMAngle(rz);
  hX.setClean();
  hY.setClean();
  hZ.setClean();

  return MStatus::kSuccess;
}
