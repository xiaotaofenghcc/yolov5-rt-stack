#include "cxxopts.hpp"
#include <iostream>
#include <memory>
#include <chrono>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <ATen/ATen.h>
#include <torch/script.h>
#include <torch/torch.h>

#include <torchvision/vision.h>
#include <torchvision/ROIPool.h>
#include <torchvision/nms.h>

std::vector<std::string> LoadNames(const std::string& path) {
  // load class names
  std::vector<std::string> class_names;
  std::ifstream infile(path);
  if (infile.is_open()) {
    std::string line;
    while (getline (infile, line)) {
      class_names.emplace_back(line);
    }
    infile.close();
  }
  else {
    std::cerr << "Error loading the class names!\n";
  }

  return class_names;
}

torch::Tensor ReadImage(const std::string& loc) {
  // Read Image from the location of image
  cv::Mat img = cv::imread(loc);
  img.convertTo(img, CV_32FC3, 1.0f / 255.0f); // normalization 1/255

  // Convert image to tensor
  torch::Tensor img_tensor = torch::from_blob(img.data, {img.rows, img.cols, 3});
  img_tensor = img_tensor.permute({2, 0, 1}); // Channels x Height x Width

  return img_tensor.clone();
};

int main(int argc, const char* argv[]) {
  cxxopts::Options parser(argv[0], "A LibTorch inference implementation of the yolov5");

  // TODO: add other args
  parser.allow_unrecognised_options().add_options()
      ("checkpoint", "yolov5.torchscript.pt path", cxxopts::value<std::string>())
      ("input_source", "image source to be detected", cxxopts::value<std::string>())
      ("labelmap", "label of datasets", cxxopts::value<std::string>())
      ("gpu", "Enable cuda device or cpu", cxxopts::value<bool>()->default_value("false"))
      ("view_img", "display results", cxxopts::value<bool>()->default_value("false"))
      ("h,help", "Print usage");

  auto opt = parser.parse(argc, argv);

  if (opt.count("help")) {
    std::cout << parser.help() << std::endl;
    exit(0);
  }

  // check if gpu flag is set
  bool is_gpu = opt["gpu"].as<bool>();

  // set device type - CPU/GPU
  torch::DeviceType device_type;
  if (torch::cuda::is_available() && is_gpu) {
    std::cout << ">>> Set GPU mode" << std::endl;
    device_type = torch::kCUDA;
  } else {
    std::cout << ">>> Set CPU mode" << std::endl;
    device_type = torch::kCPU;
  }

  // load class names from dataset for visualization
  std::string labelmap = opt["labelmap"].as<std::string>();
  std::vector<std::string> class_names = LoadNames(labelmap);
  if (class_names.empty()) {
    return -1;
  }

  // load input image
  std::string image_path = opt["input_source"].as<std::string>();

  torch::jit::script::Module module;
  try {
    std::cout << ">>> Loading model" << std::endl;
    // Deserialize the ScriptModule from a file using torch::jit::load().
    std::string weights = opt["checkpoint"].as<std::string>();
    module = torch::jit::load(weights);
    module.to(device_type);
    std::cout << ">>> Model loaded" << std::endl;
  } catch (const torch::Error& e) {
    std::cout << ">>> error loading the model" << std::endl;
    return -1;
  } catch (const std::exception& e) {
    std::cout << ">>> Other error: " << e.what() << std::endl;
    return -1;
  }

  // TorchScript models require a List[IValue] as input
  std::vector<torch::jit::IValue> inputs;

  // YOLO accepts a List[Tensor] as main input
  std::vector<torch::Tensor> images;

  torch::TensorOptions options = torch::TensorOptions{device_type};

  // Run once to warm up
  std::cout << ">>> Run once on empty image" << std::endl;
  images.push_back(torch::rand({3, 416, 352}, options));
  inputs.push_back(images);

  auto output = module.forward(inputs);

  images.clear();
  inputs.clear();

  // Read image
  auto img = ReadImage(image_path);
  img = img.to(device_type);

  // Inference
  images.push_back(img);
  inputs.push_back(images);

  output = module.forward(inputs);
  auto detections = output.toTuple()->elements()[1];

  std::cout << ">>> OKey, detections: " << detections << std::endl;

  return 0;
}
