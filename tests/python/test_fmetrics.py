import importlib.metadata
import math

import numpy as np
import pytest

import fmetrics


@pytest.fixture
def images():
    reference = np.zeros((192, 192, 3), dtype=np.uint8)
    distorted = reference.copy()
    distorted[80:112, 80:112] = 16
    return reference, distorted


def test_version():
    assert fmetrics.__version__ == importlib.metadata.version(
        "image-fmetrics"
    )


def test_identical_images():
    image = np.zeros((192, 192, 3), dtype=np.uint8)
    assert fmetrics.iwssim(image, image) == pytest.approx(1.0)
    assert fmetrics.msssim(image, image) == pytest.approx(1.0)
    assert fmetrics.ssimu2(image, image) == pytest.approx(100.0)
    assert fmetrics.butteraugli(image, image) == pytest.approx(0.0)
    result = fmetrics.cvvdp(
        image,
        image,
        fmetrics.DisplayModel.STANDARD_FHD,
    )
    assert result == pytest.approx((10.0, 0.0))


def test_distorted_images(images):
    reference, distorted = images
    scores = (
        fmetrics.iwssim(reference, distorted),
        fmetrics.msssim(reference, distorted),
        fmetrics.ssimu2(reference, distorted),
        fmetrics.butteraugli(reference, distorted),
        *fmetrics.cvvdp(reference, distorted),
    )
    assert all(math.isfinite(score) for score in scores)


def test_cvvdp_sequence(images):
    reference, distorted = images
    with fmetrics.Cvvdp(
        192,
        192,
        24.0,
        display_model="standard_fhd",
        threads=2,
    ) as metric:
        first = metric.process_frame(reference, reference)
        final = metric.process_frame(reference, distorted)
        assert first == pytest.approx((10.0, 0.0))
        assert all(math.isfinite(value) for value in final)
        metric.reset()
        assert metric.process_frame(reference, reference) == pytest.approx(
            first
        )
        assert metric.process_frame(reference, distorted) == pytest.approx(
            final
        )
    with pytest.raises(RuntimeError, match="closed"):
        metric.process_frame(reference, distorted)


def test_cvvdp_sequence_dimensions(images):
    reference, distorted = images
    metric = fmetrics.Cvvdp(191, 192, 24.0)
    with pytest.raises(fmetrics.Error, match="dimensions"):
        metric.process_frame(reference, distorted)


def test_cvvdp_version():
    assert fmetrics.cvvdp_version()


def test_cvvdp_invalid_display(images):
    reference, distorted = images
    with pytest.raises(ValueError, match="unknown CVVDP display model"):
        fmetrics.cvvdp(reference, distorted, "unknown")


def test_workspace_and_maps(images):
    reference, distorted = images
    workspace = fmetrics.Workspace()
    score, error_map = workspace.ssimu2_map(reference, distorted)
    assert score == pytest.approx(workspace.ssimu2(reference, distorted))
    assert error_map.shape == (192, 192)
    assert error_map.format == "I"
    score, error_map = workspace.butteraugli_map(reference, distorted)
    assert score == pytest.approx(
        workspace.butteraugli(reference, distorted)
    )
    assert error_map.shape == (192, 192)


def test_padded_rows(images):
    reference, distorted = images
    padded_ref = np.zeros((192, 196, 3), dtype=np.uint8)
    padded_dist = padded_ref.copy()
    padded_ref[:, :192] = reference
    padded_dist[:, :192] = distorted
    assert fmetrics.ssimu2(
        padded_ref[:, :192], padded_dist[:, :192]
    ) == pytest.approx(fmetrics.ssimu2(reference, distorted))


def test_float32(images):
    reference, distorted = images
    reference = reference.astype(np.float32) / 255.0
    distorted = distorted.astype(np.float32) / 255.0
    assert math.isfinite(fmetrics.ssimu2(reference, distorted))


def test_invalid_shape():
    image = np.zeros((32, 32), dtype=np.uint8)
    with pytest.raises(ValueError, match="HxWx3"):
        fmetrics.ssimu2(image, image)


def test_unsupported_dtype():
    image = np.zeros((32, 32, 3), dtype=np.float64)
    with pytest.raises(TypeError, match="uint8, uint16, or float32"):
        fmetrics.ssimu2(image, image)


def test_native_error():
    reference = np.zeros((32, 32, 3), dtype=np.uint8)
    distorted = np.zeros((31, 32, 3), dtype=np.uint8)
    with pytest.raises(fmetrics.Error, match="dimensions"):
        fmetrics.ssimu2(reference, distorted)


def test_memoryview():
    data = bytearray(192 * 192 * 3)
    image = memoryview(data).cast("B", shape=(192, 192, 3))
    assert fmetrics.ssimu2(image, image) == pytest.approx(100.0)
