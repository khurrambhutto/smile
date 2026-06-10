#include "CameraDevice.h"

bool CameraDevice::isValid() const
{
    return !path.isEmpty() && !displayName.isEmpty() && !formats.isEmpty();
}
