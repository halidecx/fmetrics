#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "fmetrics.h"

#define WORKSPACE_CAPSULE "fmetrics.Workspace"
#define CVVDP_CAPSULE "fmetrics.Cvvdp"
#define CVVDP_CLOSED "fmetrics.Cvvdp.closed"

typedef struct ImageView {
    Py_buffer view;
    FmetricsImg image;
} ImageView;

typedef FmetricsErr (*MetricFn)(
    FmetricsWorkspace *, const FmetricsImg *, const FmetricsImg *, double *);

typedef FmetricsErr (*MapFn)(
    FmetricsWorkspace *, const FmetricsImg *, const FmetricsImg *, double *,
    uint32_t *);

static PyObject *error_type;

static void workspace_destroy(PyObject *capsule) {
    FmetricsWorkspace *workspace = PyCapsule_GetPointer(
        capsule, WORKSPACE_CAPSULE);
    if (workspace) fmetrics_workspace_destroy(workspace);
}

static FmetricsWorkspace *workspace_get(PyObject *capsule) {
    return PyCapsule_GetPointer(capsule, WORKSPACE_CAPSULE);
}

static void cvvdp_destroy(PyObject *capsule) {
    FmetricsCvvdpCtx *context = PyCapsule_GetPointer(
        capsule, CVVDP_CAPSULE);
    if (context) fmetrics_cvvdp_destroy(context);
}

static FmetricsCvvdpCtx *cvvdp_get(PyObject *capsule) {
    if (PyCapsule_IsValid(capsule, CVVDP_CAPSULE))
        return PyCapsule_GetPointer(capsule, CVVDP_CAPSULE);
    if (PyCapsule_IsValid(capsule, CVVDP_CLOSED))
        PyErr_SetString(PyExc_RuntimeError, "CVVDP context is closed");
    else PyErr_SetString(PyExc_TypeError, "expected a CVVDP context");
    return NULL;
}

static PyObject *raise_fmetrics(const FmetricsErr error) {
    PyErr_SetString(error_type, fmetrics_error_str(error));
    return NULL;
}

static int image_get(PyObject *object, ImageView *out) {
    memset(out, 0, sizeof(*out));
    const int flags = PyBUF_FORMAT | PyBUF_ND | PyBUF_STRIDES;
    if (PyObject_GetBuffer(object, &out->view, flags) < 0) return -1;
    Py_buffer *view = &out->view;
    if (view->ndim != 3 || !view->shape || view->shape[0] <= 0 ||
        view->shape[1] <= 0 || view->shape[2] != 3)
    {
        PyErr_SetString(PyExc_ValueError, "expected an HxWx3 image");
        goto fail;
    }
    if ((uint64_t)view->shape[0] > UINT32_MAX ||
        (uint64_t)view->shape[1] > UINT32_MAX || !view->strides ||
        view->strides[0] <= 0 || view->strides[0] > UINT32_MAX)
    {
        PyErr_SetString(PyExc_ValueError, "image dimensions are too large");
        goto fail;
    }
    FmetricsPixFmt format;
    FmetricsColorspace colorspace;
    if (view->itemsize == 1 && !strcmp(view->format, "B")) {
        format = FMETRICS_PIX_FMT_RGB_UINT8;
        colorspace = FMETRICS_COLORSPACE_SRGB;
    } else if (view->itemsize == 2 && !strcmp(view->format, "H")) {
        format = FMETRICS_PIX_FMT_RGB_UINT16;
        colorspace = FMETRICS_COLORSPACE_SRGB;
    } else if (view->itemsize == 4 && !strcmp(view->format, "f")) {
        format = FMETRICS_PIX_FMT_RGB_FLOAT;
        colorspace = FMETRICS_COLORSPACE_LINEAR_SRGB;
    } else {
        PyErr_SetString(
            PyExc_TypeError,
            "expected native-endian uint8, uint16, or float32 data");
        goto fail;
    }
    if (view->strides[2] != view->itemsize ||
        view->strides[1] != view->itemsize * 3)
    {
        PyErr_SetString(PyExc_ValueError, "image pixels must be interleaved");
        goto fail;
    }
    out->image = (FmetricsImg){
        .data = view->buf,
        .width = (uint32_t)view->shape[1],
        .height = (uint32_t)view->shape[0],
        .stride = (uint32_t)view->strides[0],
        .format = format,
        .colorspace = colorspace,
        .hdr = false,
    };
    return 0;

fail:
    PyBuffer_Release(view);
    memset(out, 0, sizeof(*out));
    return -1;
}

static void image_release(ImageView *image) {
    if (image->view.obj) PyBuffer_Release(&image->view);
}

static int images_get(
    PyObject *reference, PyObject *distorted,
    ImageView *ref, ImageView *dist)
{
    if (image_get(reference, ref) < 0) return -1;
    if (image_get(distorted, dist) == 0) return 0;
    image_release(ref);
    return -1;
}

static PyObject *workspace_create(
    PyObject *self, PyObject *Py_UNUSED(args))
{
    (void)self;
    FmetricsWorkspace *workspace = fmetrics_workspace_create();
    if (!workspace) return PyErr_NoMemory();
    PyObject *capsule = PyCapsule_New(
        workspace, WORKSPACE_CAPSULE, workspace_destroy);
    if (capsule) return capsule;
    fmetrics_workspace_destroy(workspace);
    return NULL;
}

static PyObject *metric(PyObject *args, const MetricFn fn)
{
    PyObject *capsule, *reference, *distorted;
    if (!PyArg_ParseTuple(
        args, "OOO", &capsule, &reference, &distorted)) return NULL;
    FmetricsWorkspace *workspace = workspace_get(capsule);
    if (!workspace) return NULL;
    ImageView ref, dist;
    if (images_get(reference, distorted, &ref, &dist) < 0) return NULL;
    double result;
    FmetricsErr error;
    Py_BEGIN_ALLOW_THREADS
    error = fn(workspace, &ref.image, &dist.image, &result);
    Py_END_ALLOW_THREADS
    image_release(&ref);
    image_release(&dist);
    if (error != FMETRICS_OK) return raise_fmetrics(error);
    return PyFloat_FromDouble(result);
}

static PyObject *map_metric(
    PyObject *args, const MapFn fn)
{
    PyObject *capsule, *reference, *distorted;
    if (!PyArg_ParseTuple(
        args, "OOO", &capsule, &reference, &distorted)) return NULL;
    FmetricsWorkspace *workspace = workspace_get(capsule);
    if (!workspace) return NULL;
    ImageView ref, dist;
    if (images_get(reference, distorted, &ref, &dist) < 0) return NULL;
    const size_t pixels =
        (size_t)ref.image.width * (size_t)ref.image.height;
    if (pixels > (size_t)PY_SSIZE_T_MAX / sizeof(uint32_t)) {
        image_release(&ref);
        image_release(&dist);
        return PyErr_NoMemory();
    }
    PyObject *data = PyByteArray_FromStringAndSize(
        NULL, (Py_ssize_t)(pixels * sizeof(uint32_t)));
    if (!data) {
        image_release(&ref);
        image_release(&dist);
        return NULL;
    }
    double result;
    FmetricsErr error;
    uint32_t *output = (uint32_t *)PyByteArray_AsString(data);
    Py_BEGIN_ALLOW_THREADS
    error = fn(workspace, &ref.image, &dist.image, &result, output);
    Py_END_ALLOW_THREADS
    image_release(&ref);
    image_release(&dist);
    if (error != FMETRICS_OK) {
        Py_DECREF(data);
        return raise_fmetrics(error);
    }
    return Py_BuildValue(
        "(dNii)", result, data, ref.image.height, ref.image.width);
}

static PyObject *py_iwssim(PyObject *self, PyObject *args) {
    (void)self;
    return metric(args, fmetrics_iwssim_cmp);
}

static PyObject *py_msssim(PyObject *self, PyObject *args) {
    (void)self;
    return metric(args, fmetrics_msssim_cmp);
}

static PyObject *py_ssimu2(PyObject *self, PyObject *args) {
    (void)self;
    return metric(args, fmetrics_ssimu2_cmp);
}

static PyObject *py_ssimu2_map(PyObject *self, PyObject *args) {
    (void)self;
    return map_metric(args, fmetrics_ssimu2_cmp_map);
}

static PyObject *py_butteraugli(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *capsule, *reference, *distorted;
    FmetricsButteraugliOptions options = {203.0f, 3};
    if (!PyArg_ParseTuple(
        args, "OOO|fi", &capsule, &reference, &distorted,
        &options.intensity_target, &options.pnorm)) return NULL;
    FmetricsWorkspace *workspace = workspace_get(capsule);
    if (!workspace) return NULL;
    ImageView ref, dist;
    if (images_get(reference, distorted, &ref, &dist) < 0) return NULL;
    double result;
    FmetricsErr error;
    Py_BEGIN_ALLOW_THREADS
    error = fmetrics_butteraugli_cmp(
        workspace, &ref.image, &dist.image, &options, &result);
    Py_END_ALLOW_THREADS
    image_release(&ref);
    image_release(&dist);
    if (error != FMETRICS_OK) return raise_fmetrics(error);
    return PyFloat_FromDouble(result);
}

static PyObject *py_butteraugli_map(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *capsule, *reference, *distorted;
    FmetricsButteraugliOptions options = {203.0f, 3};
    if (!PyArg_ParseTuple(
        args, "OOO|fi", &capsule, &reference, &distorted,
        &options.intensity_target, &options.pnorm)) return NULL;
    FmetricsWorkspace *workspace = workspace_get(capsule);
    if (!workspace) return NULL;
    ImageView ref, dist;
    if (images_get(reference, distorted, &ref, &dist) < 0) return NULL;
    const size_t pixels =
        (size_t)ref.image.width * (size_t)ref.image.height;
    if (pixels > (size_t)PY_SSIZE_T_MAX / sizeof(uint32_t)) {
        image_release(&ref);
        image_release(&dist);
        return PyErr_NoMemory();
    }
    PyObject *data = PyByteArray_FromStringAndSize(
        NULL, (Py_ssize_t)(pixels * sizeof(uint32_t)));
    if (!data) {
        image_release(&ref);
        image_release(&dist);
        return NULL;
    }
    double result;
    FmetricsErr error;
    uint32_t *output = (uint32_t *)PyByteArray_AsString(data);
    Py_BEGIN_ALLOW_THREADS
    error = fmetrics_butteraugli_cmp_map(
        workspace, &ref.image, &dist.image, &options, &result, output);
    Py_END_ALLOW_THREADS
    image_release(&ref);
    image_release(&dist);
    if (error != FMETRICS_OK) {
        Py_DECREF(data);
        return raise_fmetrics(error);
    }
    return Py_BuildValue(
        "(dNii)", result, data, ref.image.height, ref.image.width);
}

static PyObject *py_cvvdp(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *reference, *distorted;
    int display_model = FMETRICS_CVVDP_DISPLAY_STANDARD_FHD;
    unsigned threads = 0;
    if (!PyArg_ParseTuple(
        args, "OO|iI", &reference, &distorted,
        &display_model, &threads)) return NULL;
    ImageView ref, dist;
    if (images_get(reference, distorted, &ref, &dist) < 0) return NULL;
    FmetricsCvvdpResult result;
    FmetricsErr error;
    Py_BEGIN_ALLOW_THREADS
    error = fmetrics_cvvdp_cmp(
        &ref.image, &dist.image, display_model, threads, NULL, &result);
    Py_END_ALLOW_THREADS
    image_release(&ref);
    image_release(&dist);
    if (error != FMETRICS_OK) return raise_fmetrics(error);
    return Py_BuildValue("(dd)", result.jod, result.quality);
}

static PyObject *cvvdp_create(PyObject *self, PyObject *args) {
    (void)self;
    int width, height;
    double fps;
    int display_model = FMETRICS_CVVDP_DISPLAY_STANDARD_FHD;
    unsigned threads = 0;
    if (!PyArg_ParseTuple(
        args, "iid|iI", &width, &height, &fps,
        &display_model, &threads)) return NULL;
    FmetricsCvvdpCtx *context = NULL;
    FmetricsErr error;
    Py_BEGIN_ALLOW_THREADS
    error = fmetrics_cvvdp_create(
        width, height, (float)fps, display_model, threads, NULL, &context);
    Py_END_ALLOW_THREADS
    if (error != FMETRICS_OK) return raise_fmetrics(error);
    PyObject *capsule = PyCapsule_New(
        context, CVVDP_CAPSULE, cvvdp_destroy);
    if (capsule) return capsule;
    fmetrics_cvvdp_destroy(context);
    return NULL;
}

static PyObject *cvvdp_process_frame(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *capsule, *reference, *distorted;
    if (!PyArg_ParseTuple(
        args, "OOO", &capsule, &reference, &distorted)) return NULL;
    FmetricsCvvdpCtx *context = cvvdp_get(capsule);
    if (!context) return NULL;
    ImageView ref, dist;
    if (images_get(reference, distorted, &ref, &dist) < 0) return NULL;
    FmetricsCvvdpResult result;
    FmetricsErr error;
    Py_BEGIN_ALLOW_THREADS
    error = fmetrics_cvvdp_process_frame(
        context, &ref.image, &dist.image, &result);
    Py_END_ALLOW_THREADS
    image_release(&ref);
    image_release(&dist);
    if (error != FMETRICS_OK) return raise_fmetrics(error);
    return Py_BuildValue("(dd)", result.jod, result.quality);
}

static PyObject *cvvdp_reset(PyObject *self, PyObject *capsule) {
    (void)self;
    FmetricsCvvdpCtx *context = cvvdp_get(capsule);
    if (!context) return NULL;
    FmetricsErr error;
    Py_BEGIN_ALLOW_THREADS
    error = fmetrics_cvvdp_reset(context);
    Py_END_ALLOW_THREADS
    if (error != FMETRICS_OK) return raise_fmetrics(error);
    Py_RETURN_NONE;
}

static PyObject *cvvdp_close(PyObject *self, PyObject *capsule) {
    (void)self;
    if (PyCapsule_IsValid(capsule, CVVDP_CLOSED)) Py_RETURN_NONE;
    FmetricsCvvdpCtx *context = cvvdp_get(capsule);
    if (!context) return NULL;
    if (PyCapsule_SetDestructor(capsule, NULL) < 0) return NULL;
    if (PyCapsule_SetName(capsule, CVVDP_CLOSED) < 0) {
        PyCapsule_SetDestructor(capsule, cvvdp_destroy);
        return NULL;
    }
    fmetrics_cvvdp_destroy(context);
    Py_RETURN_NONE;
}

static PyObject *cvvdp_version(
    PyObject *self, PyObject *Py_UNUSED(args))
{
    (void)self;
    return PyUnicode_FromString(fmetrics_cvvdp_version_str());
}

static PyObject *py_version(PyObject *self, PyObject *Py_UNUSED(args)) {
    (void)self;
    return PyUnicode_FromString(fmetrics_version_str());
}

static PyMethodDef methods[] = {
    {"workspace_create", workspace_create, METH_NOARGS, NULL},
    {"iwssim", py_iwssim, METH_VARARGS, NULL},
    {"msssim", py_msssim, METH_VARARGS, NULL},
    {"ssimu2", py_ssimu2, METH_VARARGS, NULL},
    {"ssimu2_map", py_ssimu2_map, METH_VARARGS, NULL},
    {"butteraugli", py_butteraugli, METH_VARARGS, NULL},
    {"butteraugli_map", py_butteraugli_map, METH_VARARGS, NULL},
    {"cvvdp", py_cvvdp, METH_VARARGS, NULL},
    {"cvvdp_create", cvvdp_create, METH_VARARGS, NULL},
    {"cvvdp_process_frame", cvvdp_process_frame, METH_VARARGS, NULL},
    {"cvvdp_reset", cvvdp_reset, METH_O, NULL},
    {"cvvdp_close", cvvdp_close, METH_O, NULL},
    {"cvvdp_version", cvvdp_version, METH_NOARGS, NULL},
    {"version", py_version, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "_native",
    NULL,
    -1,
    methods,
};

PyMODINIT_FUNC PyInit__native(void) {
    PyObject *result = PyModule_Create(&module);
    if (!result) return NULL;
    error_type = PyErr_NewException("fmetrics.Error", NULL, NULL);
    if (!error_type) {
        Py_DECREF(result);
        return NULL;
    }
    Py_INCREF(error_type);
    if (PyModule_AddObject(result, "Error", error_type) < 0) {
        Py_DECREF(error_type);
        Py_DECREF(error_type);
        Py_DECREF(result);
        return NULL;
    }
    return result;
}
