@Library('utils@main') _

node {

  stage('Checkout'){
    checkout scm
  }

  def COMMIT_AUTHOR_EMAIL = sh (returnStdout: true, script: "git --no-pager show -s --format='%ae'").trim();
  def EQUIVALENCE_CHECK = 1;
  def isChanged = false
  stage('Build and Push Docker Image') {
    if (isDependencyInstallerChanged(env.BRANCH_NAME)) {
      def commitHash = sh(script: 'git rev-parse HEAD', returnStdout: true)
      commitHash = commitHash.replaceAll(/[^a-zA-Z0-9-]/, '')

      isChanged = true
      DOCKER_IMAGE_TAG = pushCIImage(env.BRANCH_NAME, commitHash)
    }
  }

  stage('Install Ninja') {
    sh 'curl -L https://github.com/ninja-build/ninja/releases/download/v1.10.2/ninja-linux.zip -o ninja-linux.zip'
    sh 'sudo unzip ninja-linux.zip -d /usr/local/bin/'
    sh 'sudo chmod +x /usr/local/bin/ninja'
    sh 'sudo yum -y update'
    sh 'sudo yum install -y ccache'
    // sh 'export PATH="/usr/lib/ccache:$PATH"'
    // stash includes: '/usr/local/bin/ninja', name: 'ninja-stash'
    // stash includes: '/usr/bin/ccache', name: 'ccache-stash'
    sh 'mkdir -p stashed-files'
    sh 'cp /usr/local/bin/ninja stashed-files/'
    sh 'cp /usr/bin/ccache stashed-files/'

    stash includes: 'stashed-files/*', name: 'tools-stash'
  }

  // docker.image("openroad/ubuntu22.04-dev:${DOCKER_IMAGE_TAG}").inside {
    try {
      timeout(time: 9, unit: 'HOURS') {  
        stage('Build and test') {
          Map tasks = [failFast: false]
          tasks["Local centos7 gcc"] = {
            node {
                checkout scm
                try {
                  stage('Build centos7 gcc') {
                      sh './etc/Build.sh -no-warnings';
                  }
                  stage('Check message IDs') {
                      sh 'cd src && ../etc/find_messages.py > messages.txt';
                  }
                  stage('Test centos7 gcc') {
                      parallel (
                          'Unit tests':           { sh './test/regression' },
                          'nangate45 aes':        { sh './test/regression aes_nangate45' },
                          'nangate45 gcd':        { sh './test/regression gcd_nangate45' },
                          'nangate45 tinyRocket': { sh './test/regression tinyRocket_nangate45' },
                          'sky130hd aes':         { sh './test/regression aes_sky130hd' },
                          'sky130hd gcd':         { sh './test/regression gcd_sky130hd' },
                          'sky130hd ibex':        { sh './test/regression ibex_sky130hd' },
                          'sky130hd jpeg':        { sh './test/regression jpeg_sky130hd' },
                          'sky130hs aes':         { sh './test/regression aes_sky130hs' },
                          'sky130hs gcd':         { sh './test/regression gcd_sky130hs' },
                          'sky130hs ibex':        { sh './test/regression ibex_sky130hs' },
                          'sky130hs jpeg':        { sh './test/regression jpeg_sky130hs' },
                      )
                  }
                }
                finally {
                    always {
                        sh "find . -name results -type d -exec tar zcvf {}.tgz {} ';'";
                        archiveArtifacts artifacts: '**/results.tgz', allowEmptyArchive: true;
                    }
                }
            }
          }
          tasks["Local centos7 gcc without GUI"] = {
            node {
              sleep(5)
              checkout scm
              stage('Build centos7 gcc without GUI') {
                sh './etc/Build.sh -no-warnings -no-gui -dir=build-without-gui'; 
              }
            }
          }
          tasks["C++ Unit Tests"] = {
            node {
              sleep(5)
              checkout scm
              stage('Install Ninja') {
                sh 'curl -L https://github.com/ninja-build/ninja/releases/download/v1.10.2/ninja-linux.zip -o ninja-linux.zip'
                sh 'sudo unzip ninja-linux.zip -d /usr/local/bin/'
                sh 'sudo chmod +x /usr/local/bin/ninja'
                sh 'sudo yum -y update'
                sh 'sudo yum install -y ccache'
                // sh 'export PATH="/usr/lib/ccache:$PATH"'
                // stash includes: '/usr/local/bin/ninja', name: 'ninja-stash'
                // stash includes: '/usr/bin/ccache', name: 'ccache-stash'
              }
              stage('C++ Unit Tests') {
                sh 'cmake -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -GNinja -B build .';
                sh 'cd build';
                sh 'CLICOLOR_FORCE=1 ninja build_and_test';
              }
            }
          }
          tasks["Test C++20 Compile"] = {
            node {
              checkout scm
              stage('Test C++20 Compile') {
                sh 'sudo yum install wget lsb-release gnupg'
                sh 'wget -O - [^2^][2] | sudo apt-key add -'
                sh 'sudo wget [^3^][3] -O /etc/yum.repos.d/llvm.repo'
                sh 'sudo yum update && sudo yum install clang-16'
                sh "./etc/Build.sh -compiler='clang-16' -cmake='-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20'";
              }
            }
          }
          Map matrix_axes = [
            OS: ['ubuntu20.04', 'ubuntu22.04', 'centos7'],
            COMPILER: ['gcc', 'clang'],
          ]

          for (os in matrix_axes.OS) {
            for (compiler in matrix_axes.COMPILER) {
              tasks["Docker ${os} ${compiler}"] = {
                node {
                  sleep(5)
                  checkout scm
                  stage("Pull ${os}") {
                    retry(3) {
                      try {
                        sh "docker pull openroad/${os}-dev"
                      }
                      catch (err) {
                        echo err.getMessage();
                        sh 'sleep 1m ; exit 1';
                      }
                    }
                  }
                  stage("Build docker ${os}") {
                    sh "./etc/DockerHelper.sh create -os=${os} -target=builder -compiler=${compiler}"
                  }
                  stage("Test docker ${os}") {
                    sh "./etc/DockerHelper.sh test -os=${os} -target=builder -compiler=${compiler}"
                  } 
                }
              }
            }
          }
          parallel(tasks)
        }
      }
    } finally {
      catchError(buildResult: 'FAILURE', stageResult: 'FAILURE') {
        sendEmail(env.BRANCH_NAME, COMMIT_AUTHOR_EMAIL, "")
      }
    }
  // }
}
