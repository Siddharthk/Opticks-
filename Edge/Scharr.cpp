/*
 * The information in this file is
 * Copyright(c) 2007 Ball Aerospace & Technologies Corporation
 * and is subject to the terms and conditions of the
 * GNU Lesser General Public License Version 2.1
 * The license text is available from   
 * http://www.gnu.org/licenses/lgpl.html
 */

#include "DataAccessor.h"
#include "DataAccessorImpl.h"
#include "DataRequest.h"
#include "DesktopServices.h"
#include "MessageLogResource.h"
#include "ObjectResource.h"
#include "PlugInArgList.h"
#include "PlugInManagerServices.h"
#include "PlugInRegistration.h"
#include "Progress.h"
#include "RasterDataDescriptor.h"
#include "RasterElement.h"
#include "RasterUtilities.h"
#include "SpatialDataView.h"
#include "SpatialDataWindow.h"
#include "switchOnEncoding.h"
#include "Scharr.h"
#include <limits>

REGISTER_PLUGIN_BASIC(OpticksEdgeDetection, Scharr);

namespace
{
   template<typename T>
   void edgeDetection(T* pData, DataAccessor pSrcAcc, int row, int col, int rowSize, int colSize)
   {
      int prevCol = std::max(col - 1, 0);
      int prevRow = std::max(row - 1, 0);
      int nextCol = std::min(col + 1, colSize - 1);
      int nextRow = std::min(row + 1, rowSize - 1);
      
      pSrcAcc->toPixel(prevRow, prevCol);
      VERIFYNRV(pSrcAcc.isValid());
      T upperLeftVal = *reinterpret_cast<T*>(pSrcAcc->getColumn());

      pSrcAcc->toPixel(prevRow, col);
      VERIFYNRV(pSrcAcc.isValid());
      T upVal = *reinterpret_cast<T*>(pSrcAcc->getColumn());

      pSrcAcc->toPixel(prevRow, nextCol);
      VERIFYNRV(pSrcAcc.isValid());
      T upperRightVal = *reinterpret_cast<T*>(pSrcAcc->getColumn());

      pSrcAcc->toPixel(row, prevCol);
      VERIFYNRV(pSrcAcc.isValid());
      T leftVal = *reinterpret_cast<T*>(pSrcAcc->getColumn());

      pSrcAcc->toPixel(row, nextCol);
      VERIFYNRV(pSrcAcc.isValid());
      T rightVal = *reinterpret_cast<T*>(pSrcAcc->getColumn());

      pSrcAcc->toPixel(nextRow, prevCol);
      VERIFYNRV(pSrcAcc.isValid());
      T lowerLeftVal = *reinterpret_cast<T*>(pSrcAcc->getColumn());

      pSrcAcc->toPixel(nextRow, col);
      VERIFYNRV(pSrcAcc.isValid());
      T downVal = *reinterpret_cast<T*>(pSrcAcc->getColumn());

      pSrcAcc->toPixel(nextRow, nextCol);
      VERIFYNRV(pSrcAcc.isValid());
      T lowerRightVal = *reinterpret_cast<T*>(pSrcAcc->getColumn());

	  
      double gx = 3.0 * upperLeftVal + 10.0 * leftVal + 3.0 * lowerLeftVal + -3.0 * upperRightVal + -10.0 *
         rightVal + -3.0 * lowerRightVal;
      double gy = -3.0 * lowerLeftVal + -10.0 * downVal + -3.0 * lowerRightVal + 3.0 * upperLeftVal + 10.0 *
         upVal + 3.0 * upperRightVal;
      double magnitude = sqrt(gx * gx + gy * gy);

      *pData = static_cast<T>(magnitude);
   }
};

Scharr::Scharr()
{
   setDescriptorId("{2100746F-68FD-4ABB-BF84-02C887D4F896}");
   setName("Scharr Edge Detection");
   setVersion("Sample");
   setDescription("Calculate and return an edge detection raster element for first band "
      "of the provided raster element.");
   setCreator("Opticks Community");
   setCopyright("Copyright (C) 2008, Ball Aerospace & Technologies Corp.");
   setProductionStatus(false);
   setType("Sample");
   setSubtype("Edge Detection");
   setMenuLocation("[Edge Detection]/Scharr Filter");
   setAbortSupported(true);
}

Scharr::~Scharr()
{
}

bool Scharr::getInputSpecification(PlugInArgList*& pInArgList)
{
   pInArgList = Service<PlugInManagerServices>()->getPlugInArgList();
   VERIFY(pInArgList != NULL);
   pInArgList->addArg<Progress>(Executable::ProgressArg(), NULL, "Progress reporter");
   pInArgList->addArg<RasterElement>(Executable::DataElementArg(), "Perform edge detection on this data element");
   return true;
}

bool Scharr::getOutputSpecification(PlugInArgList*& pOutArgList)
{
   pOutArgList = Service<PlugInManagerServices>()->getPlugInArgList();
   VERIFY(pOutArgList != NULL);
   pOutArgList->addArg<RasterElement>("Result", NULL);
   return true;
}

bool Scharr::execute(PlugInArgList* pInArgList, PlugInArgList* pOutArgList)
{
   StepResource pStep("Scharr Edge Detection", "app", "576E21A3-BF43-424B-8FD3-061CAA392D49");
   if (pInArgList == NULL || pOutArgList == NULL)
   {
      return false;
   }
   Progress* pProgress = pInArgList->getPlugInArgValue<Progress>(Executable::ProgressArg());
   RasterElement* pCube = pInArgList->getPlugInArgValue<RasterElement>(Executable::DataElementArg());
   if (pCube == NULL)
   {
      std::string msg = "A raster cube must be specified.";
      pStep->finalize(Message::Failure, msg);
      if (pProgress != NULL) 
      {
         pProgress->updateProgress(msg, 0, ERRORS);
      }
      return false;
   }
   RasterDataDescriptor* pDesc = static_cast<RasterDataDescriptor*>(pCube->getDataDescriptor());
   VERIFY(pDesc != NULL);
   if (pDesc->getDataType() == INT4SCOMPLEX || pDesc->getDataType() == FLT8COMPLEX)
   {
      std::string msg = "Edge detection cannot be performed on complex types.";
      pStep->finalize(Message::Failure, msg);
      if (pProgress != NULL) 
      {
         pProgress->updateProgress(msg, 0, ERRORS);
      }
      return false;
   }

   FactoryResource<DataRequest> pRequest;
   DataAccessor pSrcAcc = pCube->getDataAccessor(pRequest.release());

   ModelResource<RasterElement> pResultCube(RasterUtilities::createRasterElement(pCube->getName() +
      "_Edge_Detection_Result", pDesc->getRowCount(), pDesc->getColumnCount(), pDesc->getDataType()));
   if (pResultCube.get() == NULL)
   {
      std::string msg = "A raster cube could not be created.";
      pStep->finalize(Message::Failure, msg);
      if (pProgress != NULL) 
      {
         pProgress->updateProgress(msg, 0, ERRORS);
      }
      return false;
   }
   FactoryResource<DataRequest> pResultRequest;
   pResultRequest->setWritable(true);
   DataAccessor pDestAcc = pResultCube->getDataAccessor(pResultRequest.release());

   for (unsigned int row = 0; row < pDesc->getRowCount(); ++row)
   {
      if (pProgress != NULL)
      {
         pProgress->updateProgress("Calculating result", row * 100 / pDesc->getRowCount(), NORMAL);
      }
      if (isAborted())
      {
         std::string msg = getName() + " has been aborted.";
         pStep->finalize(Message::Abort, msg);
         if (pProgress != NULL)
         {
            pProgress->updateProgress(msg, 0, ABORT);
         }
         return false;
      }
      if (!pDestAcc.isValid())
      {
         std::string msg = "Unable to access the cube data.";
         pStep->finalize(Message::Failure, msg);
         if (pProgress != NULL) 
         {
            pProgress->updateProgress(msg, 0, ERRORS);
         }
         return false;
      }
      for (unsigned int col = 0; col < pDesc->getColumnCount(); ++col)
      {
         switchOnEncoding(pDesc->getDataType(), edgeDetection, pDestAcc->getColumn(), pSrcAcc, row, col,
            pDesc->getRowCount(), pDesc->getColumnCount());
         pDestAcc->nextColumn();
      }

      pDestAcc->nextRow();
   }

   if (!isBatch())
   {
      Service<DesktopServices> pDesktop;

      SpatialDataWindow* pWindow = static_cast<SpatialDataWindow*>(pDesktop->createWindow(pResultCube->getName(),
         SPATIAL_DATA_WINDOW));

      SpatialDataView* pView = (pWindow == NULL) ? NULL : pWindow->getSpatialDataView();
      if (pView == NULL)
      {
         std::string msg = "Unable to create view.";
         pStep->finalize(Message::Failure, msg);
         if (pProgress != NULL) 
         {
            pProgress->updateProgress(msg, 0, ERRORS);
         }
         return false;
      }

      pView->setPrimaryRasterElement(pResultCube.get());
      pView->createLayer(RASTER, pResultCube.get());
   }

   if (pProgress != NULL)
   {
      pProgress->updateProgress("Edge Detection is complete.", 100, NORMAL);
   }

   pOutArgList->setPlugInArgValue("Scharr_Edge_Result", pResultCube.release());

   pStep->finalize();
   return true;
}
