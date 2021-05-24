#pragma once
#ifndef DCM_READER
#define DCM_READER

#define NOMINMAX
#include <Windows.h>
#include <imebra/imebra.h>
#include <string>

void readDcm(std::string path) {
	// read dicom

	
	imebra::DataSet loadedDataSet(imebra::CodecFactory::load(path));
	imebra::Image image(loadedDataSet.getImageApplyModalityTransform(0));
	// https://stackoverflow.com/questions/60942986/c-reading-16bit-binary-data-from-raw-img-and-store-them-in-vector

}

#endif