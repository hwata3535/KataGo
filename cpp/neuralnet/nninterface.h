#ifndef NEURALNET_NNINTERFACE_H_
#define NEURALNET_NNINTERFACE_H_

#include "../core/global.h"
#include "../core/commontypes.h"
#include "../core/hash.h"
#include "../core/logger.h"
#include "../neuralnet/desc.h"
#include "../neuralnet/nninputs.h"

// A handle to cross-thread cross-gpu initialization state.
// Create one of these per process, although creating more is fine.
struct ComputeContext;

// A handle to the local compute backend. Not thread-safe, each handle should
// only be used by one thread.
struct ComputeHandle;

// The interface for the input buffers for the neural network. The MCTS code
// uses this interface to pass data into the neural network for computation.
struct InputBuffers;

// A handle to the loaded neural network model.
struct LoadedModel;

// Generic interface to neural net inference.
// There is a single CUDA backend.
namespace NeuralNet {
  // Call globalInitialize() once upon program startup to construct the net.
  void globalInitialize();
  // Call globalCleanup() at program termination.
  void globalCleanup();

  // Model I/O -----------------------------------------------------------------

  LoadedModel* loadModelFile(const std::string& file);
  void freeLoadedModel(LoadedModel* loadedModel);

  std::string getModelName(const LoadedModel* loadedModel);
  int getModelVersion(const LoadedModel* loadedModel);

  //Return the "nearest" supported ruleset to desiredRules by this model.
  //Fills supported with true if desiredRules itself was exactly supported, false if some modifications had to be made.
  Rules getSupportedRules(const LoadedModel* loadedModel, const Rules& desiredRules, bool& supported);

  // Context -------------------------------------------------------------------

  ComputeContext* createComputeContext(
    //The indices of all gpus that this context will be used for.
    //-1 as an entry indicates to select a default
    const std::vector<int>& gpuIdxs,
    Logger* logger,
    int nnXLen,
    int nnYLen,
    std::string openCLTunerFile,
    bool openCLReTunePerBoardSize,
    enabled_t useFP16Mode,
    enabled_t useNHWCMode,
    const LoadedModel* loadedModel
  );
  //A ComputeContext should NOT be freed until all ComputeHandles created using it have also been freed.
  void freeComputeContext(ComputeContext* computeContext);

  // Compute Handle -----------------------------------------------------------------

  // Any given thread should only ever create one of these at a time.
  // When using the CUDA backend, will mutably set the GPU that this thread is
  // associated with to the specified index. If logger is specified, may output
  // some info messages to it. If requireExactNNLen is true, the backend is
  // allowed to assume that all boards to evaluate will be of size exactly equal
  // to (nnXLen,nnYLen) rather than smaller, and skip any masking operations.
  // gpuIdxForThisThread == -1 indicates to select a default GPU.
  ComputeHandle* createComputeHandle(
    ComputeContext* context,
    const LoadedModel* loadedModel,
    Logger* logger,
    int maxBatchSize,
    bool requireExactNNLen,
    bool inputsUseNHWC,
    int gpuIdxForThisThread
  );
  void freeComputeHandle(ComputeHandle* computeHandle);

  //Input Buffers ---------------------------------------------------------------

  InputBuffers* createInputBuffers(const LoadedModel* loadedModel, int maxBatchSize, int nnXLen, int nnYLen);
  void freeInputBuffers(InputBuffers* buffers);

  //The neural net takes in 2 tensors as input.
  //One of them ("spatial") is 3-dimensional per-batch-element (4-dimensional including the batch dimension N),
  //containing floats for the the values of different features (C) across the space of the board (H,W),
  //such as placement of stones and prior move locations.

  //The other ("global") is 1-dimensional per-batch-element containing floats for features that are
  //global to the board state, such as game rules and komi.

  // Returns a pointer to a float array of size getBatchEltSpatialLen() = H * W * C in
  // NHWC or NCHW format that can be filled with the spatial input features.
  float* getBatchEltSpatialInplace(InputBuffers* buffers, int nIdx);
  // Returns a pointer to a float array of size getBatchEltGlobalLen() that can be
  // filled with the global input features.
  float* getBatchEltGlobalInplace(InputBuffers* buffers, int nIdx);

  // Returns a pointer to bool array of length 3 to input the board symmetries that should
  // be used to rotate/reflect the board for the neural net.
  bool* getSymmetriesInplace(InputBuffers* buffers);

  // The total number of spatial features ("C"), times nnYLen ("H"), times nnXLen ("W")
  int getBatchEltSpatialLen(const InputBuffers* buffers);
  // The total number of global features
  int getBatchEltGlobalLen(const InputBuffers* buffers);

  //Perform Neural Net Evals ---------------------------------------------------------

  // Preconditions:
  // buffers has been filled with input data for all values of nIdx in [0,numBatchEltsFilled-1]
  // outputs has length numBatchEltsFilled containing allocated but possibly-uninitialized NNOutput structs.

  // Result: mutably writes the results of the numBatchEltsFilled many parallel neural net evaluations
  // into the NNOutput structs.
  // All outputs are in logits - all final activation functions softmax, tanh, etc. are NOT applied.
  void getOutput(ComputeHandle* computeHandle, InputBuffers* buffers, int numBatchEltsFilled, std::vector<NNOutput*>& outputs);


  //FOR TESTING -----------------------------------------------------------------------
  //For all of the below, the input buffers must have exactly the size expected of the input for the operation.
  //If useNHWC, assumes inputBuffer and outputBuffer are NHWC format, else assumes NCHW format.

  //If the operation is implemented for testing, a backend should return true and evaluate the
  //specific operation on the input buffer, resizing the output buffer and writing the result.
  //If it is not implemented, backend should return false.

  bool testEvaluateConv(
    const ConvLayerDesc* desc,
    int batchSize,
    int nnXLen,
    int nnYLen,
    bool useFP16,
    bool useNHWC,
    const std::vector<float>& inputBuffer,
    std::vector<float>& outputBuffer
  );

  //Mask should be in 'NHW' format (no "C" channel).
  bool testEvaluateBatchNorm(
    const BatchNormLayerDesc* desc,
    int batchSize,
    int nnXLen,
    int nnYLen,
    bool useFP16,
    bool useNHWC,
    const std::vector<float>& inputBuffer,
    const std::vector<float>& maskBuffer,
    std::vector<float>& outputBuffer
  );

  bool testEvaluateResidualBlock(
    const ResidualBlockDesc* desc,
    int batchSize,
    int nnXLen,
    int nnYLen,
    bool useFP16,
    bool useNHWC,
    const std::vector<float>& inputBuffer,
    const std::vector<float>& maskBuffer,
    std::vector<float>& outputBuffer
  );

  bool testEvaluateGlobalPoolingResidualBlock(
    const GlobalPoolingResidualBlockDesc* desc,
    int batchSize,
    int nnXLen,
    int nnYLen,
    bool useFP16,
    bool useNHWC,
    const std::vector<float>& inputBuffer,
    const std::vector<float>& maskBuffer,
    std::vector<float>& outputBuffer
  );

  bool testEvaluateSymmetry(
    int batchSize,
    int numChannels,
    int nnXLen,
    int nnYLen,
    bool useFP16,
    bool useNHWC,
    const bool* symmetries,
    const std::vector<float>& inputBuffer,
    std::vector<float>& outputBuffer
  );

}  // namespace NeuralNet


#endif  // NEURALNET_NNINTERFACE_H_
