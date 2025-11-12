# Copyright (c) 2025 ByteDance Ltd. and/or its affiliates
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Authors:
#   Jinlong Xuan <15563983051@163.com>
#   Xu Ji <sov.matrixac@gmail.com>
#   Yu Wang <wangyu.steph@bytedance.com>
#   Bo Liu <liubo.2024@bytedance.com>
#   Zhenwei Pi <pizhenwei@bytedance.com>
#   Rui Zhang <zhangrui.1203@bytedance.com>
#   Changqi Lu <luchangqi.123@bytedance.com>
#   Enhua Zhou <zhouenhua@bytedance.com>

import priskv
import argparse


class PriskvExample():

    def __init__(self, args):
        self.raddr = args.raddr
        self.rport = args.rport
        self.laddr = args.laddr
        self.lport = args.lport

        ######## PriskvClient ########
        raw_client = priskv.PriskvClient(self.raddr, self.rport, self.laddr,
                                     self.lport, 1)
        assert (raw_client != 0)

        self.raw_client = raw_client

    def get_ptr(self):
        pass

    def run_example(self):
        # set sgl
        self.reg = self.raw_client.reg_memory(self.get_ptr(),
                                              self.reg_buf_bytes)
        self.sgl = priskv.SGL(self.get_ptr(), self.reg_buf_bytes, self.reg)

        # set key-value
        # device -> database
        ret = self.raw_client.set("key", self.sgl)
        assert (ret == 0)

        # test key
        ret = self.raw_client.test("key")
        assert (ret == True)

    def clear(self):
        # delete key
        ret = self.raw_client.delete("key")
        if ret != 0:
            print("delete key failed")

        # dereg mr
        self.raw_client.dereg_memory(self.reg)

        # close conn
        self.raw_client.close()


class PriskvTorchExample(PriskvExample):

    def __init__(self, args):
        import torch

        super().__init__(args)

        # register mr
        self.reg_buf = torch.rand((1024 * 4), dtype=torch.float32).to("cuda:0")
        self.reg_buf_bytes = self.reg_buf.element_size() * self.reg_buf.numel()

    def get_ptr(self):
        super().get_ptr()
        return self.reg_buf.data_ptr()

    def run_example(self):
        import torch

        super().run_example()

        # clear reg_buf
        reg_buf_copy = self.reg_buf.clone()
        self.reg_buf.zero_()

        # get key-value
        # database -> device
        ret = self.raw_client.get("key", self.sgl, self.reg_buf_bytes)
        assert (ret == 0)

        # data consistency verification
        assert (torch.equal(reg_buf_copy, self.reg_buf))

        self.run_priskv_tensor_client()

    def run_priskv_tensor_client(self):
        import torch

        ######## PriskvTensorClient ########
        # set
        tensor_client = priskv.PriskvTensorClient(self.raddr, self.rport,
                                              self.laddr, self.lport, 1)
        assert (tensor_client != 0)
        set_buf = torch.rand((1024 * 4), dtype=torch.float32).to("cuda:0")
        ret = tensor_client.set("key", set_buf)
        assert (ret == 0)

        # get
        get_buf = torch.zeros((1024 * 4), dtype=torch.float32).to("cuda:0")
        ret = tensor_client.get("key", get_buf)
        assert (ret == 0)

        # data consistency verification
        assert (torch.equal(get_buf, set_buf))

        # close conn
        tensor_client.close()


class PriskvNumpyExample(PriskvExample):

    def __init__(self, args):
        import numpy as np

        super().__init__(args)

        # register mr
        self.reg_buf = np.random.rand((1024 * 4)).astype(np.float32)
        self.reg_buf_bytes = self.reg_buf.nbytes

    def get_ptr(self):
        super().get_ptr()
        return self.reg_buf.ctypes.data

    def run_example(self):
        import numpy as np
        super().run_example()

        new_reg_buf = np.zeros((1024 * 4)).astype(np.float32)
        new_reg_buf_bytes = new_reg_buf.nbytes
        assert (new_reg_buf_bytes == self.reg_buf_bytes)

        new_reg = self.raw_client.reg_memory(new_reg_buf.ctypes.data,
                                             new_reg_buf_bytes)
        new_sgl = priskv.SGL(new_reg_buf.ctypes.data, new_reg_buf_bytes, new_reg)

        # get key-value
        # database -> device
        ret = self.raw_client.get("key", new_sgl, new_reg_buf_bytes)
        assert (ret == 0)

        # data consistency verification
        assert (np.array_equal(self.reg_buf, new_reg_buf))

        self.raw_client.dereg_memory(new_reg)


def example(args):
    if args.torch:
        priskv_example = PriskvTorchExample(args)
    else:
        priskv_example = PriskvNumpyExample(args)

    priskv_example.run_example()
    priskv_example.clear()

    print("test priskv success!")


def main():
    parser = argparse.ArgumentParser(description='Priskv Example')
    parser.add_argument("--raddr",
                        type=str,
                        required=True,
                        help="remote address")

    parser.add_argument("--rport",
                        type=int,
                        default=18512,
                        help="remote port, default 18512")

    parser.add_argument(
        "--laddr",
        type=str,
        default=None,
        help="local address, default None (auto chosen by system)")

    parser.add_argument("--lport",
                        type=int,
                        default=0,
                        help="local port, default 0 (any)")

    parser.add_argument("--torch", action="store_true", default=False)

    args = parser.parse_args()
    example(args)


if __name__ == "__main__":
    main()
