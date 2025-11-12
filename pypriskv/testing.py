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


class PriskvClientTesting:

    def __init__(self, raddr: str, rport: int):
        import numpy as np
        self.client = priskv.PriskvClient(raddr, rport, None, 0, 1)
        self.key = "priskv-testing-key"
        self.sendbuf = np.random.rand((1024 * 4)).astype(np.float32)
        self.sendmr = self.client.reg_memory(self.sendbuf.ctypes.data,
                                             self.sendbuf.nbytes)
        assert self.sendmr != 0

        self.recvbuf = np.zeros((1024 * 4)).astype(np.float32)
        self.recvmr = self.client.reg_memory(self.recvbuf.ctypes.data,
                                             self.recvbuf.nbytes)
        assert self.recvmr != 0

    def set(self):
        assert self.client.set(
            self.key,
            priskv.SGL(self.sendbuf.ctypes.data, self.sendbuf.nbytes,
                     self.sendmr), 1) == 0

    def get(self):
        assert self.client.get(
            self.key,
            priskv.SGL(self.recvbuf.ctypes.data, self.recvbuf.nbytes,
                     self.recvmr), 1) == 0

    def verify(self):
        import numpy as np
        assert np.array_equal(self.sendbuf, self.recvbuf)

    def test(self) -> bool:
        return self.client.test(self.key)

    def keys(self):
        keys = self.client.keys(self.key)
        assert len(keys) == 1
        assert keys[0] == self.key

    def nrkeys(self):
        assert self.client.nrkeys(self.key) == 1

    def delete(self):
        assert self.client.delete(self.key) == 0

    def cleanup(self):
        self.client.dereg_memory(self.sendmr)
        self.client.dereg_memory(self.recvmr)
        self.client.close()


class PriskvClientTensorTesting:

    def __init__(self, raddr: str, rport: int):
        import torch
        self.client = priskv.PriskvTensorClient(raddr, rport, None, 0, 1)
        self.key = "priskv-testing-key"
        self.sendtensor = torch.rand((1024 * 4), dtype=torch.float32)
        self.recvtensor = torch.zeros((1024 * 4), dtype=torch.float32)

    def set(self):
        assert self.client.set(self.key, self.sendtensor) == 0

    def get(self):
        assert self.client.get(self.key, self.recvtensor) == 0

    def verify(self):
        import torch
        assert torch.equal(self.sendtensor, self.recvtensor)

    def test(self) -> bool:
        return self.client.test(self.key)

    def keys(self):
        keys = self.client.keys(self.key)
        assert len(keys) == 1
        assert keys[0] == self.key

    def nrkeys(self):
        assert self.client.nrkeys(self.key) == 1

    def delete(self):
        assert self.client.delete(self.key) == 0

    def cleanup(self):
        self.client.close()


def run_testing(testing):
    testing.set()
    testing.get()
    testing.verify()
    assert testing.test() == True
    testing.keys()
    testing.nrkeys()
    testing.delete()
    assert testing.test() == False
    testing.cleanup()


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

    parser.add_argument("--torch", action="store_true", default=False)

    args = parser.parse_args()

    if (args.torch):
        testing = PriskvClientTensorTesting(args.raddr, args.rport)
    else:
        testing = PriskvClientTesting(args.raddr, args.rport)

    run_testing(testing)


if __name__ == "__main__":
    main()
